#include "chat/chat_service.hpp"
#include "provider/language_model_provider.hpp"

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
    REQUIRE(request.messages.size() == 1);
    REQUIRE(request.messages[0].role == ChatRole::User);
    REQUIRE(request.messages[0].content == "hello");

    if (stop_token.stop_requested()) {
      return;
    }
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = "hi"});
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = " there"});
  }
};

std::vector<ChatEvent> SubmitAndCollect(ChatService& service,
                                        const std::string& message) {
  std::mutex mutex;
  std::condition_variable condition;
  std::vector<ChatEvent> events;
  bool finished = false;

  service.SubmitUserMessage(message, [&](ChatEvent event) {
    std::lock_guard lock(mutex);
    if (event.type == ChatEventType::Finished ||
        event.type == ChatEventType::Cancelled) {
      finished = true;
    }
    events.push_back(std::move(event));
    condition.notify_one();
  });

  std::unique_lock lock(mutex);
  condition.wait(lock, [&] { return finished; });
  return events;
}

}  // namespace

TEST_CASE("ChatService streams provider events and records history") {
  ProviderRegistry registry;
  registry.Register(std::make_shared<FakeProvider>());
  ChatService service(std::move(registry), "fake", "fake-model");

  const auto events = SubmitAndCollect(service, "hello");

  REQUIRE(events.size() == 5);
  REQUIRE(events[0].type == ChatEventType::Started);
  REQUIRE(events[1].type == ChatEventType::TextDelta);
  REQUIRE(events[1].text == "hi");
  REQUIRE(events[2].type == ChatEventType::TextDelta);
  REQUIRE(events[2].text == " there");
  REQUIRE(events[3].type == ChatEventType::AssistantMessageDone);
  REQUIRE(events[4].type == ChatEventType::Finished);

  const auto history = service.History();
  REQUIRE(history.size() == 2);
  REQUIRE(history[0].role == ChatRole::User);
  REQUIRE(history[0].content == "hello");
  REQUIRE(history[1].role == ChatRole::Assistant);
  REQUIRE(history[1].content == "hi there");
}

TEST_CASE("ChatService emits error for missing provider") {
  ProviderRegistry registry;
  ChatService service(std::move(registry), "missing", "fake-model");

  std::vector<ChatEvent> events;
  service.SubmitUserMessage("hello",
                            [&](ChatEvent event) { events.push_back(event); });

  REQUIRE(events.size() == 2);
  REQUIRE(events[0].type == ChatEventType::Error);
  REQUIRE(events[1].type == ChatEventType::Finished);
}
