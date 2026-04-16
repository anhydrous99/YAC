#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "provider/language_model_provider.hpp"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

namespace {

class FakeProvider : public LanguageModelProvider {
 public:
  explicit FakeProvider(std::string expected_model = "fake-model")
      : expected_model_(std::move(expected_model)) {}

  [[nodiscard]] std::string Id() const override { return "fake"; }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override {
    {
      std::lock_guard lock(mutex_);
      requests_.push_back(request);
    }
    REQUIRE(request.model == expected_model_);
    REQUIRE(request.stream);
    REQUIRE(!request.messages.empty());
    REQUIRE(request.messages.back().role == ChatRole::User);

    if (stop_token.stop_requested()) {
      return;
    }
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = "hi"});
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = " there"});
  }

  [[nodiscard]] ChatRequest LastRequest() const {
    std::lock_guard lock(mutex_);
    REQUIRE_FALSE(requests_.empty());
    return requests_.back();
  }

 private:
  std::string expected_model_;
  mutable std::mutex mutex_;
  std::vector<ChatRequest> requests_;
};

class ToolRoundProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "tool-round"; }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override {
    if (stop_token.stop_requested()) {
      return;
    }
    int call_index = 0;
    {
      std::lock_guard lock(mutex_);
      requests_.push_back(request);
      call_index = static_cast<int>(requests_.size());
    }

    if (call_index == 1) {
      REQUIRE_FALSE(request.tools.empty());
      sink(ChatEvent{.type = ChatEventType::ToolCallRequested,
                     .tool_calls = {ToolCallRequest{
                         .id = "tool_1",
                         .name = "list_dir",
                         .arguments_json = R"({"path":"."})"}}});
      return;
    }

    REQUIRE(std::any_of(request.messages.begin(), request.messages.end(),
                        [](const ChatMessage& message) {
                          return message.role == ChatRole::Tool &&
                                 message.tool_call_id == "tool_1";
                        }));
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = "listed"});
  }

  [[nodiscard]] size_t RequestCount() const {
    std::lock_guard lock(mutex_);
    return requests_.size();
  }

 private:
  mutable std::mutex mutex_;
  std::vector<ChatRequest> requests_;
};

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
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = "done"});
  }

  void WaitUntilStarted() {
    std::unique_lock lock(mutex_);
    cv_.wait(lock, [&] { return started_; });
  }

  void Release() {
    std::lock_guard lock(mutex_);
    release_ = true;
    cv_.notify_one();
  }

  [[nodiscard]] std::string LastRequestModel() const {
    std::lock_guard lock(mutex_);
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
      std::lock_guard lock(mutex_);
      started_ = true;
      cv_.notify_one();
    }

    while (!stop_token.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    std::lock_guard lock(mutex_);
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

class ErrorFakeProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "error-fake"; }

  void CompleteStream([[maybe_unused]] const ChatRequest& request,
                      ChatEventSink sink,
                      [[maybe_unused]] std::stop_token stop_token) override {
    sink(ChatEvent{.type = ChatEventType::Error, .text = "stream failed"});
  }
};

class EmptyDeltaProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "empty-delta"; }

  void CompleteStream([[maybe_unused]] const ChatRequest& request,
                      ChatEventSink sink,
                      [[maybe_unused]] std::stop_token stop_token) override {
    sink(ChatEvent{.type = ChatEventType::TextDelta});
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = "ok"});
    sink(ChatEvent{.type = ChatEventType::TextDelta});
  }
};

std::vector<ChatEvent> CollectEvents(ChatService& service,
                                     const std::string& message) {
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<ChatEvent> events;
  bool finished = false;
  service.SetEventCallback([&](ChatEvent event) {
    std::lock_guard lock(mutex);
    events.push_back(std::move(event));
    if (events.back().type == ChatEventType::Finished) {
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
    registry.Register(std::make_shared<FakeProvider>());
    config.provider_id = "fake";
    config.model = "fake-model";
  }
  return ChatService(std::move(registry), config);
}

bool HasEvent(const std::vector<ChatEvent>& events, ChatEventType type) {
  return std::any_of(events.begin(), events.end(),
                     [type](const auto& e) { return e.type == type; });
}

const ChatEvent& FindEvent(const std::vector<ChatEvent>& events,
                           ChatEventType type) {
  auto it = std::find_if(events.begin(), events.end(),
                         [type](const auto& e) { return e.type == type; });
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
  REQUIRE(queued.role == ChatRole::User);
  REQUIRE(queued.text == "hello");
  REQUIRE(started.role == ChatRole::Assistant);
  REQUIRE(started.message_id != queued.message_id);

  const auto history = service.History();
  REQUIRE(history.size() == 2);
  REQUIRE(history[0].role == ChatRole::User);
  REQUIRE(history[0].content == "hello");
  REQUIRE(history[1].role == ChatRole::Assistant);
  REQUIRE(history[1].content == "hi there");
  REQUIRE(history[1].id == started.message_id);
}

TEST_CASE("ChatService executes a non-mutating tool round") {
  auto root = std::filesystem::temp_directory_path() / "yac_tool_round";
  std::filesystem::remove_all(root);
  std::filesystem::create_directories(root);
  {
    std::ofstream file(root / "note.txt");
    file << "hello\n";
  }

  auto provider = std::make_shared<ToolRoundProvider>();
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
  REQUIRE(std::any_of(history.begin(), history.end(), [](const auto& message) {
    return message.role == ChatRole::Tool && message.tool_call_id == "tool_1" &&
           message.content.find("note.txt") != std::string::npos;
  }));
  std::filesystem::remove_all(root);
}

TEST_CASE("ChatService drops empty streaming deltas") {
  auto service = MakeService(std::make_shared<EmptyDeltaProvider>());
  const auto events = CollectEvents(service, "hello");

  const auto text_delta_count =
      std::count_if(events.begin(), events.end(), [](const ChatEvent& event) {
        return event.type == ChatEventType::TextDelta;
      });

  REQUIRE(text_delta_count == 1);
  const auto& delta = FindEvent(events, ChatEventType::TextDelta);
  REQUIRE(delta.text == "ok");
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
    std::lock_guard lock(mtx);
    events.push_back(event);
    if (event.type == ChatEventType::Finished) {
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
  REQUIRE(error.role == ChatRole::Assistant);
  REQUIRE(error.message_id != queued.message_id);
}

TEST_CASE("ChatService preserves provider stream error status") {
  auto service = MakeService(std::make_shared<ErrorFakeProvider>());
  const auto events = CollectEvents(service, "hello");

  REQUIRE(HasEvent(events, ChatEventType::Error));
  REQUIRE_FALSE(HasEvent(events, ChatEventType::AssistantMessageDone));

  const auto& error = FindEvent(events, ChatEventType::Error);
  REQUIRE(error.role == ChatRole::Assistant);
  REQUIRE(error.status == ChatMessageStatus::Error);

  const auto history = service.History();
  REQUIRE(history.size() == 1);
  REQUIRE(history[0].role == ChatRole::User);
}

TEST_CASE("ChatService includes system prompt before history in requests") {
  auto provider = std::make_shared<FakeProvider>();
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
  auto provider = std::make_shared<FakeProvider>("glm-5.1");
  ChatConfig config;
  config.provider_id = "fake";
  config.model = "fake-model";
  auto service = MakeService(provider, config);

  std::vector<ChatEvent> events;
  std::mutex mtx;
  std::condition_variable cv;
  bool finished = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::lock_guard lock(mtx);
    events.push_back(std::move(event));
    if (events.back().type == ChatEventType::Finished) {
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
  REQUIRE(event.provider_id == "fake");
  REQUIRE(event.model == "glm-5.1");
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
    std::lock_guard lock(mtx);
    events.push_back(std::move(event));
    if (events.back().type == ChatEventType::Finished) {
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
  REQUIRE(event.model == "second-model");
}

TEST_CASE("ChatService cancellation requests provider stop token") {
  auto provider = std::make_shared<CancellableFakeProvider>();
  auto service = MakeService(provider);

  std::vector<ChatEvent> events;
  std::mutex mtx;
  std::condition_variable cv;
  bool cancelled = false;

  service.SetEventCallback([&](ChatEvent event) {
    std::lock_guard lock(mtx);
    events.push_back(std::move(event));
    if (events.back().type == ChatEventType::MessageStatusChanged &&
        events.back().status == ChatMessageStatus::Cancelled) {
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
    std::lock_guard lock(mtx);
    events.push_back(std::move(event));
  });

  auto id1 = service.SubmitUserMessage("first");
  auto id2 = service.SubmitUserMessage("second");

  REQUIRE(id1 != id2);
  REQUIRE(service.QueueDepth() >= 0);

  blocking->Release();

  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  std::lock_guard lock(mtx);
  bool has_queued =
      std::any_of(events.begin(), events.end(), [](const ChatEvent& e) {
        return e.type == ChatEventType::UserMessageQueued;
      });
  REQUIRE(has_queued);
}

TEST_CASE("ChatService ResetConversation clears history and pending") {
  auto service = MakeService();

  std::vector<ChatEvent> events;
  std::mutex mtx;

  service.SetEventCallback([&](ChatEvent event) {
    std::lock_guard lock(mtx);
    events.push_back(std::move(event));
  });

  service.SubmitUserMessage("hello");

  service.ResetConversation();

  REQUIRE(service.History().empty());
  REQUIRE_FALSE(service.IsBusy());

  std::lock_guard lock(mtx);
  REQUIRE(HasEvent(events, ChatEventType::ConversationCleared));
}

TEST_CASE("ChatService assigns unique message IDs") {
  auto service = MakeService();

  std::vector<ChatMessageId> ids;
  std::mutex mtx;
  std::condition_variable cv;
  int finished_count = 0;

  service.SetEventCallback([&](ChatEvent event) {
    std::lock_guard lock(mtx);
    if (event.type == ChatEventType::UserMessageQueued) {
      ids.push_back(event.message_id);
    }
    if (event.type == ChatEventType::Finished) {
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

TEST_CASE("LoadChatConfigFromEnv returns defaults when no env vars set") {
  auto config = LoadChatConfigFromEnv();
  REQUIRE(config.provider_id == "openai");
  REQUIRE(config.model == "gpt-4o-mini");
  REQUIRE(config.temperature == 0.7);
  REQUIRE_FALSE(config.system_prompt.has_value());
}

TEST_CASE("LoadChatConfigFromEnv applies Z.ai provider defaults") {
  const auto original_dir = std::filesystem::current_path();

  const auto temp_dir =
      std::filesystem::temp_directory_path() / "test_env_config_zai_defaults";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::current_path(temp_dir);

  const std::string env_content =
      "YAC_PROVIDER=zai\n"
      "ZAI_API_KEY=zai-api-key\n";

  std::ofstream env_file(temp_dir / ".env");
  env_file << env_content;
  env_file.close();

  auto config = LoadChatConfigFromEnv();

  REQUIRE(config.provider_id == "zai");
  REQUIRE(config.model == "glm-5.1");
  REQUIRE(config.base_url == "https://api.z.ai/api/coding/paas/v4");
  REQUIRE(config.api_key_env == "ZAI_API_KEY");
  REQUIRE(config.api_key == "zai-api-key");

  std::filesystem::current_path(original_dir);
  std::filesystem::remove_all(temp_dir);
}

TEST_CASE("LoadChatConfigFromEnv explicit values override Z.ai defaults") {
  const auto original_dir = std::filesystem::current_path();

  const auto temp_dir =
      std::filesystem::temp_directory_path() / "test_env_config_zai_overrides";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::current_path(temp_dir);

  const std::string env_content =
      "YAC_PROVIDER=zai\n"
      "YAC_MODEL=glm-4.7\n"
      "YAC_BASE_URL=https://zai.example.com/v1\n"
      "YAC_API_KEY_ENV=YAC_CUSTOM_ZAI_KEY\n"
      "YAC_CUSTOM_ZAI_KEY=custom-zai-key\n";

  std::ofstream env_file(temp_dir / ".env");
  env_file << env_content;
  env_file.close();

  auto config = LoadChatConfigFromEnv();

  REQUIRE(config.provider_id == "zai");
  REQUIRE(config.model == "glm-4.7");
  REQUIRE(config.base_url == "https://zai.example.com/v1");
  REQUIRE(config.api_key_env == "YAC_CUSTOM_ZAI_KEY");
  REQUIRE(config.api_key == "custom-zai-key");

  std::filesystem::current_path(original_dir);
  std::filesystem::remove_all(temp_dir);
}

TEST_CASE("LoadChatConfigFromEnv loads from .env file") {
  const auto original_dir = std::filesystem::current_path();

  const auto temp_dir =
      std::filesystem::temp_directory_path() / "test_env_config";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::current_path(temp_dir);

  const std::string env_content =
      "YAC_PROVIDER=env-provider\n"
      "YAC_MODEL=env-model\n"
      "YAC_BASE_URL=https://env.example.com/v1/\n"
      "YAC_TEMPERATURE=1.5\n"
      "YAC_API_KEY_ENV=YAC_TEST_API_KEY_FROM_FILE\n"
      "YAC_TEST_API_KEY_FROM_FILE=env-api-key\n"
      "YAC_SYSTEM_PROMPT=\"Env system prompt\"\n";

  std::ofstream env_file(temp_dir / ".env");
  env_file << env_content;
  env_file.close();

  auto config = LoadChatConfigFromEnv();

  REQUIRE(config.provider_id == "env-provider");
  REQUIRE(config.model == "env-model");
  REQUIRE(config.base_url == "https://env.example.com/v1/");
  REQUIRE(config.temperature == 1.5);
  REQUIRE(config.api_key_env == "YAC_TEST_API_KEY_FROM_FILE");
  REQUIRE(config.api_key == "env-api-key");
  REQUIRE(config.system_prompt.has_value());
  REQUIRE(config.system_prompt == std::string{"Env system prompt"});

  std::filesystem::current_path(original_dir);
  std::filesystem::remove_all(temp_dir);
}

TEST_CASE("LoadChatConfigFromEnv: environment variables override .env") {
  const auto original_dir = std::filesystem::current_path();

  const auto temp_dir =
      std::filesystem::temp_directory_path() / "test_env_override";
  std::filesystem::create_directories(temp_dir);
  std::filesystem::current_path(temp_dir);

  const std::string env_content =
      "YAC_PROVIDER=env-provider\n"
      "YAC_MODEL=env-model\n"
      "YAC_TEMPERATURE=1.5\n"
      "YAC_API_KEY_ENV=YAC_TEST_API_KEY_OVERRIDE\n"
      "YAC_TEST_API_KEY_OVERRIDE=env-api-key\n";

  std::ofstream env_file(temp_dir / ".env");
  env_file << env_content;
  env_file.close();

#ifdef _WIN32
  _putenv_s("YAC_PROVIDER", "system-provider");
  _putenv_s("YAC_MODEL", "system-model");
  _putenv_s("YAC_TEMPERATURE", "0.8");
  _putenv_s("YAC_TEST_API_KEY_OVERRIDE", "system-api-key");
#else
  setenv("YAC_PROVIDER", "system-provider", 1);
  setenv("YAC_MODEL", "system-model", 1);
  setenv("YAC_TEMPERATURE", "0.8", 1);
  setenv("YAC_TEST_API_KEY_OVERRIDE", "system-api-key", 1);
#endif

  auto config = LoadChatConfigFromEnv();

  REQUIRE(config.provider_id == "system-provider");
  REQUIRE(config.model == "system-model");
  REQUIRE(config.temperature == 0.8);
  REQUIRE(config.api_key_env == "YAC_TEST_API_KEY_OVERRIDE");
  REQUIRE(config.api_key == "system-api-key");

#ifdef _WIN32
  _putenv_s("YAC_PROVIDER", "");
  _putenv_s("YAC_MODEL", "");
  _putenv_s("YAC_TEMPERATURE", "");
  _putenv_s("YAC_TEST_API_KEY_OVERRIDE", "");
#else
  unsetenv("YAC_PROVIDER");
  unsetenv("YAC_MODEL");
  unsetenv("YAC_TEMPERATURE");
  unsetenv("YAC_TEST_API_KEY_OVERRIDE");
#endif

  std::filesystem::current_path(original_dir);
  std::filesystem::remove_all(temp_dir);
}
