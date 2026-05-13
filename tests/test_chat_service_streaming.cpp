#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "core_types/typed_ids.hpp"
#include "lambda_mock_provider.hpp"
#include "provider/language_model_provider.hpp"
#include "util/wait_until.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
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

std::shared_ptr<LambdaMockProvider> MakeFakeProvider(
    ::yac::ModelId expected_model = ::yac::ModelId{"fake-model"}) {
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
                     message.tool_call_id == yac::ToolCallId{"tool_1"};
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

  [[nodiscard]] ::yac::ModelId LastRequestModel() const {
    std::scoped_lock lock(mutex_);
    return request_model_;
  }

 private:
  mutable std::mutex mutex_;
  std::condition_variable cv_;
  ::yac::ModelId request_model_;
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
      // SLEEP-RATIONALE: provider polls stop_token without busy-waiting
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
                     message.tool_call_id == yac::ToolCallId{"tool_1"} &&
                     message.content.find("Path is outside the workspace") !=
                         std::string::npos;
            }));
        sink(ChatEvent{TextDeltaEvent{.text = "recovered"}});
      });
}

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
    if (config.provider_id.value == "openai-compatible") {
      config.provider_id = ::yac::ProviderId{provider->Id()};
    }
    registry.Register(std::move(provider));
  } else {
    registry.Register(MakeFakeProvider());
    config.provider_id = ::yac::ProviderId{"fake"};
    config.model = ::yac::ModelId{"fake-model"};
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
  config.provider_id = ::yac::ProviderId{"tool-round"};
  config.model = ::yac::ModelId{"fake-model"};
  config.workspace_root = root.string();
  auto service = MakeService(provider, config);

  const auto events = CollectEvents(service, "list files");

  REQUIRE(provider->RequestCount() == 2);
  REQUIRE(HasEvent(events, ChatEventType::ToolCallStarted));
  REQUIRE(HasEvent(events, ChatEventType::ToolCallDone));
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));

  const auto history = service.History();
  REQUIRE(std::ranges::any_of(history, [](const auto& message) {
    return message.role == ChatRole::Tool &&
           message.tool_call_id == yac::ToolCallId{"tool_1"} &&
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
  config.provider_id = ::yac::ProviderId{"infinite-tools"};
  config.model = ::yac::ModelId{"fake-model"};
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

TEST_CASE(
    "ChatService keeps tool error results in history for follow-up round") {
  auto root = std::filesystem::temp_directory_path() / "yac_tool_error_round";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);

  auto provider = MakeToolErrorProvider();
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"tool-error"};
  config.model = ::yac::ModelId{"fake-model"};
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
  config.provider_id = ::yac::ProviderId{"missing"};
  config.model = ::yac::ModelId{"fake-model"};
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

TEST_CASE("ChatService SetModel updates future requests and emits event") {
  auto provider = MakeFakeProvider(::yac::ModelId{"glm-5.1"});
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"fake"};
  config.model = ::yac::ModelId{"fake-model"};
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

  service.SetModel(::yac::ModelId{"glm-5.1"});
  service.SubmitUserMessage("hello");

  std::unique_lock lock(mtx);
  cv.wait(lock, [&] { return finished; });

  const auto request = provider->LastRequest();
  REQUIRE(request.model.value == "glm-5.1");
  const auto& event = FindEvent(events, ChatEventType::ModelChanged);
  REQUIRE(event.Get<ModelChangedEvent>().provider_id.value == "fake");
  REQUIRE(event.Get<ModelChangedEvent>().model.value == "glm-5.1");
}

TEST_CASE("ChatService SetModel does not mutate active request snapshot") {
  auto provider = std::make_shared<BlockingFakeProvider>();
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"blocking-fake"};
  config.model = ::yac::ModelId{"first-model"};
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
  service.SetModel(::yac::ModelId{"second-model"});

  REQUIRE(provider->LastRequestModel().value == "first-model");

  provider->Release();
  std::unique_lock lock(mtx);
  cv.wait(lock, [&] { return finished; });

  const auto& event = FindEvent(events, ChatEventType::ModelChanged);
  REQUIRE(event.Get<ModelChangedEvent>().model.value == "second-model");
}

TEST_CASE("ChatService cancellation requests provider stop token") {
  auto provider = std::make_shared<CancellableFakeProvider>();

  std::vector<ChatEvent> events;
  std::mutex mtx;
  std::condition_variable cv;
  bool cancelled = false;

  // service must be declared after the locals it captures by reference so its
  // destructor (which joins the worker thread) runs before mtx/events are
  // destroyed; otherwise the worker's FinishedEvent callback races the
  // destruction of mtx and triggers EINVAL on macOS.
  auto service = MakeService(provider);

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

  REQUIRE(yac::test::WaitUntil(
      [&] {
        std::scoped_lock lock(mtx);
        return std::count_if(events.begin(), events.end(),
                             [](const ChatEvent& e) {
                               return e.Type() == ChatEventType::Finished;
                             }) >= 2;
      },
      std::chrono::milliseconds{5000}));

  std::scoped_lock lock(mtx);
  REQUIRE(std::ranges::any_of(events, [](const ChatEvent& e) {
    return e.Type() == ChatEventType::UserMessageQueued;
  }));
}

TEST_CASE(
    "ChatService Reset returns within budget when provider blocks "
    "indefinitely") {
  auto blocking = std::make_shared<BlockingFakeProvider>();
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{blocking->Id()};
  config.model = ::yac::ModelId{"any"};

  ProviderRegistry registry;
  registry.Register(blocking);
  ChatService service(std::move(registry), config);

  constexpr auto kBudget = std::chrono::milliseconds(150);
  service.SetResetDrainBudgetForTest(kBudget);

  service.SubmitUserMessage("blocked");
  blocking->WaitUntilStarted();

  const auto t0 = std::chrono::steady_clock::now();
  service.ResetConversation();
  const auto elapsed = std::chrono::steady_clock::now() - t0;

  // Reset must observe the budget. Allow generous slack — CI clocks are
  // noisy, but anything under ~5x the budget proves we didn't hang on
  // the blocking provider.
  REQUIRE(elapsed < kBudget * 5);
  REQUIRE(service.History().empty());

  // Release the provider so the destructor can join the worker. The
  // round resumes against a stale generation and the per-append guards
  // make sure no append leaks into the cleared history.
  blocking->Release();
  // SLEEP-RATIONALE: stability check — verify no stale-generation append leaks
  // into cleared history
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  REQUIRE(service.History().empty());
}

TEST_CASE(
    "ChatService Reset drains quickly when provider observes stop_token") {
  auto cancellable = std::make_shared<CancellableFakeProvider>();
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{cancellable->Id()};
  config.model = ::yac::ModelId{"any"};

  ProviderRegistry registry;
  registry.Register(cancellable);
  ChatService service(std::move(registry), config);

  // Generous budget; we expect to return well before it expires.
  constexpr auto kBudget = std::chrono::seconds(2);
  service.SetResetDrainBudgetForTest(
      std::chrono::duration_cast<std::chrono::milliseconds>(kBudget));

  service.SubmitUserMessage("cancellable");
  cancellable->WaitUntilStarted();

  const auto t0 = std::chrono::steady_clock::now();
  service.ResetConversation();
  const auto elapsed = std::chrono::steady_clock::now() - t0;

  REQUIRE(elapsed < kBudget);
  REQUIRE(service.History().empty());
  cancellable->WaitUntilStopObserved();
}

TEST_CASE("ChatService Reset survives concurrent SubmitUserMessage") {
  auto service = MakeService();
  service.SetResetDrainBudgetForTest(std::chrono::milliseconds(200));

  std::atomic<bool> stop{false};
  std::thread submitter([&] {
    int i = 0;
    while (!stop.load(std::memory_order_relaxed)) {
      service.SubmitUserMessage("submit-" + std::to_string(i++));
      // SLEEP-RATIONALE: stress test pacing — throttles submit rate to exercise
      // concurrent-reset races
      std::this_thread::sleep_for(std::chrono::microseconds(50));
    }
  });

  for (int i = 0; i < 25; ++i) {
    service.ResetConversation();
    // SLEEP-RATIONALE: stress test pacing — yields between resets to let
    // submitter thread progress
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }

  stop.store(true, std::memory_order_relaxed);
  submitter.join();

  // One final reset to absorb any in-flight prompt, then assert clean.
  service.ResetConversation();
  REQUIRE(service.History().empty());
}
