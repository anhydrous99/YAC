#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "provider/language_model_provider.hpp"

#include <algorithm>
#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

namespace {

class FakeProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "fake"; }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override {
    REQUIRE(request.model == "fake-model");
    REQUIRE(request.stream);
    REQUIRE(!request.messages.empty());
    REQUIRE(request.messages.back().role == ChatRole::User);

    if (stop_token.stop_requested()) {
      return;
    }
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = "hi"});
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = " there"});
  }
};

class BlockingFakeProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "blocking-fake"; }

  void CompleteStream(const ChatRequest&, ChatEventSink sink,
                      std::stop_token) override {
    std::unique_lock lock(mutex_);
    started_ = true;
    cv_.notify_one();
    cv_.wait(lock, [&] { return release_; });
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

 private:
  std::mutex mutex_;
  std::condition_variable cv_;
  bool started_ = false;
  bool release_ = false;
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

}  // namespace

TEST_CASE("ChatService streams provider events and records history") {
  auto service = MakeService();
  const auto events = CollectEvents(service, "hello");

  REQUIRE(HasEvent(events, ChatEventType::Started));
  REQUIRE(HasEvent(events, ChatEventType::TextDelta));
  REQUIRE(HasEvent(events, ChatEventType::AssistantMessageDone));
  REQUIRE(HasEvent(events, ChatEventType::Finished));

  const auto history = service.History();
  REQUIRE(history.size() == 2);
  REQUIRE(history[0].role == ChatRole::User);
  REQUIRE(history[0].content == "hello");
  REQUIRE(history[1].role == ChatRole::Assistant);
  REQUIRE(history[1].content == "hi there");
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
