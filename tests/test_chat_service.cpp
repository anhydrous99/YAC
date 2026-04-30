#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "lambda_mock_provider.hpp"
#include "provider/language_model_provider.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using yac::testing::LambdaMockProvider;

namespace {

// RAII guard that saves and clears YAC_* env vars, restoring them on
// destruction. Prevents the host environment from polluting config tests.
class ScopedEnvClear {
 public:
  ScopedEnvClear() {
    static constexpr std::array kVars = {"YAC_PROVIDER",
                                         "YAC_MODEL",
                                         "YAC_BASE_URL",
                                         "YAC_TEMPERATURE",
                                         "YAC_MAX_TOOL_ROUNDS",
                                         "YAC_API_KEY_ENV",
                                         "YAC_SYSTEM_PROMPT",
                                         "YAC_WORKSPACE_ROOT",
                                         "OPENAI_API_KEY",
                                         "ZAI_API_KEY",
                                         "YAC_CUSTOM_ZAI_KEY",
                                         "YAC_TEST_API_KEY_FROM_FILE",
                                         "YAC_TEST_API_KEY_OVERRIDE"};
    for (const auto* name : kVars) {
      if (const char* val = std::getenv(name)) {
        saved_.emplace_back(name, val);
        unsetenv(name);
      }
    }
  }
  ~ScopedEnvClear() {
    for (const auto& [name, val] : saved_) {
      setenv(name.c_str(), val.c_str(), 1);
    }
  }
  ScopedEnvClear(const ScopedEnvClear&) = delete;
  ScopedEnvClear& operator=(const ScopedEnvClear&) = delete;
  ScopedEnvClear(ScopedEnvClear&&) = delete;
  ScopedEnvClear& operator=(ScopedEnvClear&&) = delete;

 private:
  std::vector<std::pair<std::string, std::string>> saved_;
};

std::shared_ptr<LambdaMockProvider> MakeFakeProvider(
    std::string expected_model = "fake-model") {
  return std::make_shared<LambdaMockProvider>(
      "fake", [expected_model = std::move(expected_model)](
                  const ChatRequest& request, ChatEventSink sink,
                  std::stop_token stop_token) {
        REQUIRE(request.model == expected_model);
        REQUIRE(request.stream);
        REQUIRE(!request.messages.empty());
        REQUIRE(request.messages.back().role == ChatRole::User);
        if (stop_token.stop_requested()) {
          return;
        }
        sink(ChatEvent{TextDeltaEvent{.text = "hi"}});
        sink(ChatEvent{TextDeltaEvent{.text = " there"}});
      });
}

std::shared_ptr<LambdaMockProvider> MakeToolRoundProvider() {
  return std::make_shared<LambdaMockProvider>(
      "tool-round", [](const ChatRequest& request, ChatEventSink sink,
                       std::stop_token stop_token) {
        if (stop_token.stop_requested()) {
          return;
        }
        // Branch on whether the prior round already returned a tool result.
        const bool has_tool_result = std::ranges::any_of(
            request.messages, [](const ChatMessage& message) {
              return message.role == ChatRole::Tool &&
                     message.tool_call_id == "tool_1";
            });
        if (!has_tool_result) {
          REQUIRE_FALSE(request.tools.empty());
          sink(ChatEvent{ToolCallRequestedEvent{
              .tool_calls = {
                  ToolCallRequest{.id = "tool_1",
                                  .name = "list_dir",
                                  .arguments_json = R"({"path":"."})"}}}});
          return;
        }
        sink(ChatEvent{TextDeltaEvent{.text = "listed"}});
      });
}

std::shared_ptr<LambdaMockProvider> MakeInfiniteToolLoopProvider(
    std::shared_ptr<std::atomic<int>> call_count) {
  return std::make_shared<LambdaMockProvider>(
      "infinite-tools",
      [call_count = std::move(call_count)](
          const ChatRequest&, ChatEventSink sink, std::stop_token) {
        const int call_index = call_count->fetch_add(1);
        sink(ChatEvent{ToolCallRequestedEvent{
            .tool_calls = {
                ToolCallRequest{.id = "tool_" + std::to_string(call_index),
                                .name = "list_dir",
                                .arguments_json = R"({"path":"."})"}}}});
      });
}

class BlockingFakeProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "blocking-fake"; }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override {
    (void)stop_token;
    {
      std::unique_lock lock(mutex_);
      request_model_ = request.model;
      started_ = true;
      cv_.notify_one();
      cv_.wait(lock, [&] { return release_; });
    }
    sink(ChatEvent{TextDeltaEvent{.text = "done"}});
  }

  void WaitUntilStarted() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return started_; });
  }

  void Release() {
    std::scoped_lock lock(mutex_);
    release_ = true;
    cv_.notify_one();
  }

  [[nodiscard]] std::string LastRequestModel() const {
    std::scoped_lock lock(mutex_);
    return request_model_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  std::string request_model_;
  bool started_ = false;
  bool release_ = false;
};

class CancellableFakeProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "cancellable-fake"; }

  void CompleteStream([[maybe_unused]] const ChatRequest& request,
                      [[maybe_unused]] ChatEventSink sink,
                      std::stop_token stop_token) override {
    {
      std::scoped_lock lock(mutex_);
      started_ = true;
      cv_.notify_one();
    }

    while (!stop_token.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::scoped_lock lock(mutex_);
    stop_observed_ = true;
    cv_.notify_one();
  }

  void WaitUntilStarted() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return started_; });
  }

  void WaitUntilStopObserved() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return stop_observed_; });
  }

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool started_ = false;
  bool stop_observed_ = false;
};

std::shared_ptr<LambdaMockProvider> MakeErrorFakeProvider() {
  return std::make_shared<LambdaMockProvider>(
      "error-fake",
      [](const ChatRequest&, ChatEventSink sink, std::stop_token) {
        sink(ChatEvent{ErrorEvent{.text = "stream failed"}});
      });
}

std::shared_ptr<LambdaMockProvider> MakeEmptyDeltaProvider() {
  return std::make_shared<LambdaMockProvider>(
      "empty-delta",
      [](const ChatRequest&, ChatEventSink sink, std::stop_token) {
        sink(ChatEvent{TextDeltaEvent{}});
        sink(ChatEvent{TextDeltaEvent{.text = "ok"}});
        sink(ChatEvent{TextDeltaEvent{}});
      });
}

std::shared_ptr<LambdaMockProvider> MakeApprovalRejectionProvider() {
  auto request_count = std::make_shared<int>(0);
  return std::make_shared<LambdaMockProvider>(
      "approval-rejection",
      [request_count](const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) {
        if (stop_token.stop_requested()) {
          return;
        }
        ++(*request_count);
        if (*request_count == 1) {
          sink(ChatEvent{ToolCallRequestedEvent{
              .tool_calls = {ToolCallRequest{
                  .id = "tool_1",
                  .name = "file_write",
                  .arguments_json =
                      R"({"filepath":"notes.txt","content":"denied\n"})"}}}});
          return;
        }
        REQUIRE(std::ranges::any_of(
            request.messages, [](const ChatMessage& message) {
              return message.role == ChatRole::Tool &&
                     message.tool_call_id == "tool_1" &&
                     message.content ==
                         R"({"error":"User rejected tool execution."})";
            }));
        sink(ChatEvent{TextDeltaEvent{.text = "continued after rejection"}});
      });
}

std::shared_ptr<LambdaMockProvider> MakeToolErrorProvider() {
  auto request_count = std::make_shared<int>(0);
  return std::make_shared<LambdaMockProvider>(
      "tool-error",
      [request_count](const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) {
        if (stop_token.stop_requested()) {
          return;
        }
        ++(*request_count);
        if (*request_count == 1) {
          sink(ChatEvent{ToolCallRequestedEvent{
              .tool_calls = {
                  ToolCallRequest{.id = "tool_1",
                                  .name = "list_dir",
                                  .arguments_json = R"({"path":"../"})"}}}});
          return;
        }
        REQUIRE(std::ranges::any_of(
            request.messages, [](const ChatMessage& message) {
              return message.role == ChatRole::Tool &&
                     message.tool_call_id == "tool_1" &&
                     message.content.find("Path is outside the workspace") !=
                         std::string::npos;
            }));
        sink(ChatEvent{TextDeltaEvent{.text = "recovered"}});
      });
}

class SequentialApprovalProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override {
    return "sequential-approval";
  }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override {
    if (stop_token.stop_requested()) {
      return;
    }

    ++request_count_;
    if (request_count_ == 1) {
      sink(ChatEvent{ToolCallRequestedEvent{
          .tool_calls = {
              ToolCallRequest{
                  .id = "tool_1",
                  .name = "file_write",
                  .arguments_json =
                      R"({"filepath":"first.txt","content":"one\n"})"},
              ToolCallRequest{
                  .id = "tool_2",
                  .name = "file_write",
                  .arguments_json =
                      R"({"filepath":"second.txt","content":"two\n"})"},
          }}});
      return;
    }

    REQUIRE(std::count_if(request.messages.begin(), request.messages.end(),
                          [](const ChatMessage& message) {
                            return message.role == ChatRole::Tool;
                          }) == 2);
    sink(ChatEvent{TextDeltaEvent{.text = "all approvals resolved"}});
  }

 private:
  int request_count_ = 0;
};

std::vector<ChatEvent> CollectEvents(ChatService& service,
                                     const std::string& message) {
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<ChatEvent> events;
  bool finished = false;
  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mutex);
    events.push_back(std::move(event));
    if (events.back().Type() == ChatEventType::Finished) {
      finished = true;
      condition.notify_one();
    }
  });

  service.SubmitUserMessage(message);

  std::unique_lock lock(mutex);
  condition.wait(lock, [&] { return finished; });
  return events;
}

ChatService MakeService(
    std::shared_ptr<LanguageModelProvider> provider = nullptr,
    ChatConfig config = {}) {
  ProviderRegistry registry;
  if (provider) {
    if (config.provider_id == "openai") {
      config.provider_id = provider->Id();
    }
    registry.Register(std::move(provider));
  } else {
    registry.Register(MakeFakeProvider());
    config.provider_id = "fake";
    config.model = "fake-model";
  }
  return ChatService(std::move(registry), config);
}

bool HasEvent(const std::vector<ChatEvent>& events, ChatEventType type) {
  return std::ranges::any_of(
      events, [type](const auto& e) { return e.Type() == type; });
}

const ChatEvent& FindEvent(const std::vector<ChatEvent>& events,
                           ChatEventType type) {
  auto it = std::ranges::find_if(
      events, [type](const auto& e) { return e.Type() == type; });
  REQUIRE(it != events.end());
  return *it;
}

}  // namespace

TEST_CASE("ChatService streams provider events and records history") {
  auto service = MakeService();
  const auto events = CollectEvents(service, "hello");

  REQUIRE(HasEvent(events, ChatEventType::Started));
  REQUIRE(HasEvent(events, ChatEventType::TextDelta));
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));
  REQUIRE(HasEvent(events, ChatEventType::Finished));

  const auto& queued = FindEvent(events, ChatEventType::UserMessageQueued);
  const auto& started = FindEvent(events, ChatEventType::Started);
  const auto& queued_event = queued.Get<UserMessageQueuedEvent>();
  const auto& started_event = started.Get<StartedEvent>();
  REQUIRE(queued_event.role == ChatRole::User);
  REQUIRE(queued_event.text == "hello");
  REQUIRE(started_event.role == ChatRole::Assistant);
  REQUIRE(started_event.message_id != queued_event.message_id);

  const auto history = service.History();
  REQUIRE(history.size() == 2);
  REQUIRE(history[0].role == ChatRole::User);
  REQUIRE(history[0].content == "hello");
  REQUIRE(history[1].role == ChatRole::Assistant);
  REQUIRE(history[1].content == "hi there");
  REQUIRE(history[1].id == started_event.message_id);
}

TEST_CASE("ChatService executes a non-mutating tool round") {
  auto root = std::filesystem::temp_directory_path() / "yac_tool_round";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  {
    std::ofstream file(root / "note.txt");
    file << "hello\n";
  }

  auto provider = MakeToolRoundProvider();
  ChatConfig config;
  config.provider_id = "tool-round";
  config.model = "fake-model";
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);

  const auto events = CollectEvents(service, "list files");

  REQUIRE(provider->RequestCount() == 2);
  REQUIRE(HasEvent(events, ChatEventType::ToolCallStarted));
  REQUIRE(HasEvent(events, ChatEventType::ToolCallDone));
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));

  const auto history = service.History();
  REQUIRE(std::ranges::any_of(history, [](const auto& message) {
    return message.role == ChatRole::Tool && message.tool_call_id == "tool_1" &&
           message.content.find("note.txt") != std::string::npos;
  }));
  std::filesystem::remove_all(root);
}

TEST_CASE("ChatService caps tool rounds and emits a single limit error") {
  auto root = std::filesystem::temp_directory_path() / "yac_tool_round_limit";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto call_count = std::make_shared<std::atomic<int>>(0);
  auto provider = MakeInfiniteToolLoopProvider(call_count);
  ChatConfig config;
  config.provider_id = "infinite-tools";
  config.model = "fake-model";
  config.workspace_root = root.string();
  config.max_tool_rounds = 3;
  auto service = MakeService(provider, config);

  const auto events = CollectEvents(service, "go");

  // When the model never stops requesting tools, the service should run
  // exactly the configured number of completions + tool rounds, then surface
  // a single "Tool round limit reached" error.
  REQUIRE(call_count->load() == config.max_tool_rounds);

  const auto tool_done_count =
      std::count_if(events.begin(), events.end(), [](const ChatEvent& e) {
        return e.Type() == ChatEventType::ToolCallDone;
      });
  REQUIRE(tool_done_count == config.max_tool_rounds);

  REQUIRE_FALSE(HasEvent(events, ChatEventType::AssistantMessageDone));

  const auto error_it = std::ranges::find_if(events, [](const ChatEvent& e) {
    return e.Type() == ChatEventType::Error;
  });
  REQUIRE(error_it != events.end());
  const auto* err = error_it->As<ErrorEvent>();
  REQUIRE(err != nullptr);
  REQUIRE(err->text.find("Tool round limit reached") != std::string::npos);
  REQUIRE(err->text.find("3 rounds") != std::string::npos);

  std::filesystem::remove_all(root);
}

TEST_CASE("ChatService records rejected approval as tool error and continues") {
  auto root =
      std::filesystem::temp_directory_path() / "yac_tool_approval_rejection";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto provider = MakeApprovalRejectionProvider();
  ChatConfig config;
  config.provider_id = "approval-rejection";
  config.model = "fake-model";
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);

  std::vector<ChatEvent> events;
  std::mutex mutex;
  std::condition_variable cv;
  std::string approval_id;
  bool approval_requested = false;
  bool finished = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mutex);
    if (const auto* approval = event.As<ToolApprovalRequestedEvent>()) {
      approval_id = approval->approval_id;
      approval_requested = true;
      cv.notify_all();
    }
    if (event.Type() == ChatEventType::Finished) {
      finished = true;
      cv.notify_all();
    }
    events.push_back(std::move(event));
  });

  service.SubmitUserMessage("write file");

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return approval_requested; });
  }

  service.ResolveToolApproval(approval_id, false);

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return finished; });
  }

  REQUIRE_FALSE(std::filesystem::exists(root / "notes.txt"));
  const auto& tool_done = FindEvent(events, ChatEventType::ToolCallDone);
  REQUIRE(tool_done.Get<ToolCallDoneEvent>().status ==
          ChatMessageStatus::Error);
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));
  REQUIRE(service.History().back().content == "continued after rejection");
  std::filesystem::remove_all(root);
}

TEST_CASE("ChatService sequences approval requests one tool at a time") {
  auto root =
      std::filesystem::temp_directory_path() / "yac_tool_approval_sequencing";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto provider = std::make_shared<SequentialApprovalProvider>();
  ChatConfig config;
  config.provider_id = "sequential-approval";
  config.model = "fake-model";
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);

  std::vector<ChatEvent> events;
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> approval_ids;
  bool finished = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mutex);
    if (const auto* approval = event.As<ToolApprovalRequestedEvent>()) {
      approval_ids.push_back(approval->approval_id);
      cv.notify_all();
    }
    if (event.Type() == ChatEventType::Finished) {
      finished = true;
      cv.notify_all();
    }
    events.push_back(std::move(event));
  });

  service.SubmitUserMessage("write two files");

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return approval_ids.size() == 1; });
    REQUIRE(approval_ids.size() == 1);
  }

  service.ResolveToolApproval(approval_ids[0], true);

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return approval_ids.size() == 2; });
    REQUIRE(approval_ids.size() == 2);
  }

  service.ResolveToolApproval(approval_ids[1], true);

  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] { return finished; });
  }

  REQUIRE(std::filesystem::exists(root / "first.txt"));
  REQUIRE(std::filesystem::exists(root / "second.txt"));
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));
  std::filesystem::remove_all(root);
}

TEST_CASE(
    "ChatService keeps tool error results in history for follow-up round") {
  auto root = std::filesystem::temp_directory_path() / "yac_tool_error_round";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto provider = MakeToolErrorProvider();
  ChatConfig config;
  config.provider_id = "tool-error";
  config.model = "fake-model";
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);

  const auto events = CollectEvents(service, "list parent");

  const auto& tool_done = FindEvent(events, ChatEventType::ToolCallDone);
  REQUIRE(tool_done.Get<ToolCallDoneEvent>().status ==
          ChatMessageStatus::Error);
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));
  REQUIRE(service.History().back().content == "recovered");
  std::filesystem::remove_all(root);
}

TEST_CASE("ChatService drops empty streaming deltas") {
  auto service = MakeService(MakeEmptyDeltaProvider());
  const auto events = CollectEvents(service, "hello");

  const auto text_delta_count =
      std::count_if(events.begin(), events.end(), [](const ChatEvent& event) {
        return event.Type() == ChatEventType::TextDelta;
      });

  REQUIRE(text_delta_count == 1);
  const auto& delta = FindEvent(events, ChatEventType::TextDelta);
  REQUIRE(delta.Get<TextDeltaEvent>().text == "ok");
  REQUIRE(service.History().back().content == "ok");
}

TEST_CASE("ChatService emits error for missing provider") {
  ProviderRegistry registry;
  ChatConfig config;
  config.provider_id = "missing";
  config.model = "fake-model";
  ChatService service(std::move(registry), config);

  std::vector<ChatEvent> events;
  std::mutex mtx;
  std::condition_variable cv;
  bool done = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mtx);
    events.push_back(event);
    if (event.Type() == ChatEventType::Finished) {
      done = true;
      cv.notify_one();
    }
  });

  service.SubmitUserMessage("hello");

  std::unique_lock lock(mtx);
  cv.wait(lock, [&] { return done; });

  REQUIRE(HasEvent(events, ChatEventType::Error));
  REQUIRE(HasEvent(events, ChatEventType::Finished));
  const auto& error = FindEvent(events, ChatEventType::Error);
  const auto& queued = FindEvent(events, ChatEventType::UserMessageQueued);
  REQUIRE(error.Get<ErrorEvent>().role == ChatRole::Assistant);
  REQUIRE(error.Get<ErrorEvent>().message_id !=
          queued.Get<UserMessageQueuedEvent>().message_id);
}

TEST_CASE("ChatService preserves provider stream error status") {
  auto service = MakeService(MakeErrorFakeProvider());
  const auto events = CollectEvents(service, "hello");

  REQUIRE(HasEvent(events, ChatEventType::Error));
  REQUIRE_FALSE(HasEvent(events, ChatEventType::AssistantMessageDone));

  const auto& error = FindEvent(events, ChatEventType::Error);
  REQUIRE(error.Get<ErrorEvent>().role == ChatRole::Assistant);
  REQUIRE(error.Get<ErrorEvent>().status == ChatMessageStatus::Error);

  const auto history = service.History();
  REQUIRE(history.size() == 1);
  REQUIRE(history[0].role == ChatRole::User);
}

TEST_CASE("ChatService includes system prompt before history in requests") {
  auto provider = MakeFakeProvider();
  ChatConfig config;
  config.provider_id = "fake";
  config.model = "fake-model";
  config.system_prompt = "Use terse answers.";
  auto service = MakeService(provider, config);

  (void)CollectEvents(service, "hello");

  const auto request = provider->LastRequest();
  REQUIRE(request.messages.size() == 2);
  REQUIRE(request.messages[0].role == ChatRole::System);
  REQUIRE(request.messages[0].content == "Use terse answers.");
  REQUIRE(request.messages[1].role == ChatRole::User);
  REQUIRE(request.messages[1].content == "hello");
}

TEST_CASE("ChatService SetModel updates future requests and emits event") {
  auto provider = MakeFakeProvider("glm-5.1");
  ChatConfig config;
  config.provider_id = "fake";
  config.model = "fake-model";
  auto service = MakeService(provider, config);

  std::vector<ChatEvent> events;
  std::mutex mtx;
  std::condition_variable cv;
  bool finished = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mtx);
    events.push_back(std::move(event));
    if (events.back().Type() == ChatEventType::Finished) {
      finished = true;
      cv.notify_one();
    }
  });

  service.SetModel("glm-5.1");
  service.SubmitUserMessage("hello");

  std::unique_lock lock(mtx);
  cv.wait(lock, [&] { return finished; });

  const auto request = provider->LastRequest();
  REQUIRE(request.model == "glm-5.1");
  const auto& event = FindEvent(events, ChatEventType::ModelChanged);
  REQUIRE(event.Get<ModelChangedEvent>().provider_id == "fake");
  REQUIRE(event.Get<ModelChangedEvent>().model == "glm-5.1");
}

TEST_CASE("ChatService SetModel does not mutate active request snapshot") {
  auto provider = std::make_shared<BlockingFakeProvider>();
  ChatConfig config;
  config.provider_id = "blocking-fake";
  config.model = "first-model";
  auto service = MakeService(provider, config);

  std::vector<ChatEvent> events;
  std::mutex mtx;
  std::condition_variable cv;
  bool finished = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mtx);
    events.push_back(std::move(event));
    if (events.back().Type() == ChatEventType::Finished) {
      finished = true;
      cv.notify_one();
    }
  });

  service.SubmitUserMessage("hello");
  provider->WaitUntilStarted();
  service.SetModel("second-model");

  REQUIRE(provider->LastRequestModel() == "first-model");

  provider->Release();
  std::unique_lock lock(mtx);
  cv.wait(lock, [&] { return finished; });

  const auto& event = FindEvent(events, ChatEventType::ModelChanged);
  REQUIRE(event.Get<ModelChangedEvent>().model == "second-model");
}

TEST_CASE("ChatService cancellation requests provider stop token") {
  auto provider = std::make_shared<CancellableFakeProvider>();
  auto service = MakeService(provider);

  std::vector<ChatEvent> events;
  std::mutex mtx;
  std::condition_variable cv;
  bool cancelled = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mtx);
    events.push_back(std::move(event));
    const auto* status = events.back().As<MessageStatusChangedEvent>();
    if (status != nullptr && status->status == ChatMessageStatus::Cancelled) {
      cancelled = true;
      cv.notify_one();
    }
  });

  service.SubmitUserMessage("hello");
  provider->WaitUntilStarted();
  service.CancelActiveResponse();
  provider->WaitUntilStopObserved();

  std::unique_lock lock(mtx);
  cv.wait(lock, [&] { return cancelled; });
  REQUIRE(HasEvent(events, ChatEventType::MessageStatusChanged));
}

TEST_CASE("ChatService queues prompts while active") {
  auto blocking = std::make_shared<BlockingFakeProvider>();
  auto service = MakeService(blocking);

  std::vector<ChatEvent> events;
  std::mutex mtx;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mtx);
    events.push_back(std::move(event));
  });

  auto id1 = service.SubmitUserMessage("first");
  auto id2 = service.SubmitUserMessage("second");

  REQUIRE(id1 != id2);
  REQUIRE(service.QueueDepth() >= 0);

  blocking->Release();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::scoped_lock lock(mtx);
  bool has_queued = std::ranges::any_of(events, [](const ChatEvent& e) {
    return e.Type() == ChatEventType::UserMessageQueued;
  });
  REQUIRE(has_queued);
}

TEST_CASE("ChatService ResetConversation clears history and pending") {
  auto service = MakeService();

  std::vector<ChatEvent> events;
  std::mutex mtx;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mtx);
    events.push_back(std::move(event));
  });

  service.SubmitUserMessage("hello");

  service.ResetConversation();

  REQUIRE(service.History().empty());
  REQUIRE_FALSE(service.IsBusy());

  std::scoped_lock lock(mtx);
  REQUIRE(HasEvent(events, ChatEventType::ConversationCleared));
}

TEST_CASE("ChatService assigns unique message IDs") {
  auto service = MakeService();

  std::vector<ChatMessageId> ids;
  std::mutex mtx;
  std::condition_variable cv;
  int finished_count = 0;

  service.SetEventCallback([&](ChatEvent event) {
    std::scoped_lock lock(mtx);
    if (const auto* queued = event.As<UserMessageQueuedEvent>()) {
      ids.push_back(queued->message_id);
    }
    if (event.Type() == ChatEventType::Finished) {
      ++finished_count;
      cv.notify_one();
    }
  });

  auto id1 = service.SubmitUserMessage("first");
  auto id2 = service.SubmitUserMessage("second");

  std::unique_lock lock(mtx);
  cv.wait(lock, [&] { return finished_count >= 2; });

  REQUIRE(id1 != id2);
}

namespace {

class ScopedSettingsFile {
 public:
  explicit ScopedSettingsFile(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(path_);
  }
  ~ScopedSettingsFile() { std::filesystem::remove_all(path_); }
  ScopedSettingsFile(const ScopedSettingsFile&) = delete;
  ScopedSettingsFile& operator=(const ScopedSettingsFile&) = delete;
  ScopedSettingsFile(ScopedSettingsFile&&) = delete;
  ScopedSettingsFile& operator=(ScopedSettingsFile&&) = delete;

  void Write(std::string_view content) const {
    std::ofstream stream(path_, std::ios::trunc);
    stream << content;
  }

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace

TEST_CASE("LoadChatConfig returns defaults when settings.toml is absent") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_defaults.toml");
  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;
  REQUIRE(config.provider_id == "openai");
  REQUIRE(config.model == "gpt-4o-mini");
  REQUIRE(config.temperature == 0.7);
  REQUIRE(config.max_tool_rounds == kDefaultToolRoundLimit);
  REQUIRE_FALSE(config.system_prompt.has_value());
}

TEST_CASE(
    "LoadChatConfigResult creates settings.toml on first run when requested") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_first_run.toml");
  REQUIRE_FALSE(std::filesystem::exists(settings.Path()));
  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/true);
  REQUIRE(std::filesystem::exists(settings.Path()));
  REQUIRE(result.config.provider_id == "openai");
  REQUIRE(result.config.model == "gpt-4o-mini");
}

TEST_CASE("LoadChatConfigResult warns when API key is missing") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_missing_key.toml");

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.api_key.empty());
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Warning &&
           issue.message.find("OPENAI_API_KEY") != std::string::npos &&
           issue.detail.find("sk-") == std::string::npos;
  }));
}

TEST_CASE("YAC_TEMPERATURE env var reports invalid values") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_bad_temp_env.toml");
  setenv("YAC_TEMPERATURE", "too-hot", 1);

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.temperature == 0.7);
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message == "Invalid YAC_TEMPERATURE";
  }));

  unsetenv("YAC_TEMPERATURE");
}

TEST_CASE("settings.toml reports out-of-range temperature as an error") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_bad_temp_toml.toml");
  settings.Write("temperature = 5.0\n");

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.temperature == 0.7);
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("temperature") != std::string::npos;
  }));
}

TEST_CASE("[provider].id = zai applies the Z.ai preset") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_zai_defaults.toml");
  settings.Write(
      "[provider]\n"
      "id = \"zai\"\n");
  setenv("ZAI_API_KEY", "zai-api-key", 1);

  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;

  REQUIRE(config.provider_id == "zai");
  REQUIRE(config.model == "glm-5.1");
  REQUIRE(config.base_url == "https://api.z.ai/api/coding/paas/v4");
  REQUIRE(config.api_key_env == "ZAI_API_KEY");
  REQUIRE(config.api_key == "zai-api-key");

  unsetenv("ZAI_API_KEY");
}

TEST_CASE("TOML values override the Z.ai preset") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_zai_overrides.toml");
  settings.Write(
      "[provider]\n"
      "id = \"zai\"\n"
      "model = \"glm-4.7\"\n"
      "base_url = \"https://zai.example.com/v1\"\n"
      "api_key_env = \"YAC_CUSTOM_ZAI_KEY\"\n");
  setenv("YAC_CUSTOM_ZAI_KEY", "custom-zai-key", 1);

  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;

  REQUIRE(config.provider_id == "zai");
  REQUIRE(config.model == "glm-4.7");
  REQUIRE(config.base_url == "https://zai.example.com/v1");
  REQUIRE(config.api_key_env == "YAC_CUSTOM_ZAI_KEY");
  REQUIRE(config.api_key == "custom-zai-key");

  unsetenv("YAC_CUSTOM_ZAI_KEY");
}

TEST_CASE("settings.toml values are read end-to-end") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_full.toml");
  settings.Write(
      "temperature = 1.5\n"
      "max_tool_rounds = 44\n"
      "system_prompt = \"TOML system prompt\"\n"
      "\n"
      "[provider]\n"
      "id = \"custom-provider\"\n"
      "model = \"custom-model\"\n"
      "base_url = \"https://example.com/v1/\"\n"
      "api_key_env = \"YAC_TEST_API_KEY_FROM_FILE\"\n");
  setenv("YAC_TEST_API_KEY_FROM_FILE", "toml-api-key", 1);

  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;

  REQUIRE(config.provider_id == "custom-provider");
  REQUIRE(config.model == "custom-model");
  REQUIRE(config.base_url == "https://example.com/v1/");
  REQUIRE(config.temperature == 1.5);
  REQUIRE(config.max_tool_rounds == 44);
  REQUIRE(config.api_key_env == "YAC_TEST_API_KEY_FROM_FILE");
  REQUIRE(config.api_key == "toml-api-key");
  REQUIRE(config.system_prompt == std::string{"TOML system prompt"});

  unsetenv("YAC_TEST_API_KEY_FROM_FILE");
}

TEST_CASE("YAC_* env vars override settings.toml values") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_env_override.toml");
  settings.Write(
      "temperature = 1.5\n"
      "\n"
      "[provider]\n"
      "id = \"file-provider\"\n"
      "model = \"file-model\"\n"
      "api_key_env = \"YAC_TEST_API_KEY_OVERRIDE\"\n");

  setenv("YAC_PROVIDER", "env-provider", 1);
  setenv("YAC_MODEL", "env-model", 1);
  setenv("YAC_TEMPERATURE", "0.8", 1);
  setenv("YAC_MAX_TOOL_ROUNDS", "12", 1);
  setenv("YAC_TEST_API_KEY_OVERRIDE", "env-api-key", 1);

  auto config =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false)
          .config;

  REQUIRE(config.provider_id == "env-provider");
  REQUIRE(config.model == "env-model");
  REQUIRE(config.temperature == 0.8);
  REQUIRE(config.max_tool_rounds == 12);
  REQUIRE(config.api_key_env == "YAC_TEST_API_KEY_OVERRIDE");
  REQUIRE(config.api_key == "env-api-key");

  unsetenv("YAC_PROVIDER");
  unsetenv("YAC_MODEL");
  unsetenv("YAC_TEMPERATURE");
  unsetenv("YAC_MAX_TOOL_ROUNDS");
  unsetenv("YAC_TEST_API_KEY_OVERRIDE");
}

TEST_CASE("YAC_MAX_TOOL_ROUNDS env var reports invalid values") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_bad_tool_rounds_env.toml");
  setenv("YAC_MAX_TOOL_ROUNDS", "0", 1);

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.max_tool_rounds == kDefaultToolRoundLimit);
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message == "Invalid YAC_MAX_TOOL_ROUNDS";
  }));

  unsetenv("YAC_MAX_TOOL_ROUNDS");
}

TEST_CASE(
    "Malformed settings.toml reports an error but yields a usable "
    "config") {
  ScopedEnvClear env_guard;
  ScopedSettingsFile settings("yac_test_cfg_malformed.toml");
  settings.Write("this is =  not = valid\n");

  auto result =
      LoadChatConfigResultFrom(settings.Path(), /*create_if_missing=*/false);

  REQUIRE(result.config.provider_id == "openai");
  REQUIRE(result.config.model == "gpt-4o-mini");
  REQUIRE(std::ranges::any_of(result.issues, [](const ConfigIssue& issue) {
    return issue.severity == ConfigIssueSeverity::Error &&
           issue.message.find("settings.toml") != std::string::npos;
  }));
}
