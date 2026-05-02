#include "chat/chat_service_compactor.hpp"
#include "chat/types.hpp"
#include "lambda_mock_provider.hpp"

#include <cstddef>
#include <mutex>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::chat::internal;
using yac::testing::LambdaMockProvider;

namespace {

ChatMessage MakeMessage(ChatRole role, std::string content) {
  return ChatMessage{.role = role,
                     .status = ChatMessageStatus::Complete,
                     .content = std::move(content)};
}

std::vector<ChatMessage> MakeAlternatingHistory(std::size_t count) {
  std::vector<ChatMessage> history;
  history.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    const auto role = (i % 2 == 0) ? ChatRole::User : ChatRole::Assistant;
    history.push_back(MakeMessage(role, "message-" + std::to_string(i)));
  }
  return history;
}

ChatConfig MakeConfig(std::string mode, int keep_last) {
  ChatConfig cfg;
  cfg.auto_compact_mode = std::move(mode);
  cfg.auto_compact_keep_last = keep_last;
  return cfg;
}

LambdaMockProvider MakeIdleProvider() {
  return {"idle", [](const ChatRequest&, yac::provider::ChatEventSink,
                     std::stop_token) {}};
}

}  // namespace

TEST_CASE(
    "MaybeAutoCompactHistory truncate mode drops oldest non-system messages") {
  auto history = MakeAlternatingHistory(15);
  std::mutex history_mutex;
  auto provider = MakeIdleProvider();

  std::vector<ChatEvent> emitted;
  auto emit = [&emitted](ChatEvent event) {
    emitted.push_back(std::move(event));
  };
  const auto config = MakeConfig("truncate", 5);

  const auto outcome = MaybeAutoCompactHistory(
      history, history_mutex, config, provider, emit, std::stop_token{});

  REQUIRE(outcome.performed);
  REQUIRE(outcome.messages_removed == 10);
  REQUIRE(history.size() == 6);
  REQUIRE(history.front().role == ChatRole::System);
  REQUIRE(history.front().content.find("10 messages removed") !=
          std::string::npos);
  REQUIRE(history.back().content == "message-14");
  REQUIRE(emitted.size() == 1);
  REQUIRE(emitted[0].Type() == ChatEventType::ConversationCompacted);
  const auto& payload = emitted[0].Get<ConversationCompactedEvent>();
  REQUIRE(payload.reason == CompactReason::Auto);
  REQUIRE(payload.messages_removed == 10);
}

TEST_CASE("MaybeAutoCompactHistory is a no-op when below keep_last") {
  auto history = MakeAlternatingHistory(3);
  std::mutex history_mutex;
  auto provider = MakeIdleProvider();
  std::vector<ChatEvent> emitted;
  auto emit = [&emitted](ChatEvent event) {
    emitted.push_back(std::move(event));
  };
  const auto config = MakeConfig("truncate", 10);

  const auto outcome = MaybeAutoCompactHistory(
      history, history_mutex, config, provider, emit, std::stop_token{});

  REQUIRE_FALSE(outcome.performed);
  REQUIRE(outcome.messages_removed == 0);
  REQUIRE(history.size() == 3);
  REQUIRE(emitted.empty());
}

TEST_CASE("MaybeAutoCompactHistory preserves leading system messages") {
  std::vector<ChatMessage> history;
  history.push_back(MakeMessage(ChatRole::System, "system prompt"));
  for (std::size_t i = 0; i < 12; ++i) {
    const auto role = (i % 2 == 0) ? ChatRole::User : ChatRole::Assistant;
    history.push_back(MakeMessage(role, "message-" + std::to_string(i)));
  }
  std::mutex history_mutex;
  auto provider = MakeIdleProvider();
  std::vector<ChatEvent> emitted;
  auto emit = [&emitted](ChatEvent event) {
    emitted.push_back(std::move(event));
  };
  const auto config = MakeConfig("truncate", 5);

  const auto outcome = MaybeAutoCompactHistory(
      history, history_mutex, config, provider, emit, std::stop_token{});

  REQUIRE(outcome.performed);
  REQUIRE(history.size() == 7);
  REQUIRE(history[0].role == ChatRole::System);
  REQUIRE(history[0].content == "system prompt");
  REQUIRE(history[1].role == ChatRole::System);
  REQUIRE(history[1].content.find("7 messages removed") != std::string::npos);
}

namespace {

LambdaMockProvider MakeSummaryProvider(std::string summary_text,
                                       std::shared_ptr<int> call_count = {}) {
  return {"summarize", [summary_text = std::move(summary_text),
                        call_count = std::move(call_count)](
                           const ChatRequest& request,
                           yac::provider::ChatEventSink sink, std::stop_token) {
            if (call_count) {
              ++*call_count;
            }
            // The summarizer must call non-streaming with low temperature.
            REQUIRE_FALSE(request.stream);
            REQUIRE(request.temperature <= 0.5);
            REQUIRE(request.messages.size() == 2);
            REQUIRE(request.messages[0].role == ChatRole::System);
            sink(ChatEvent{TextDeltaEvent{.text = summary_text}});
          }};
}

LambdaMockProvider MakeErrorProvider() {
  return {"error", [](const ChatRequest&, yac::provider::ChatEventSink sink,
                      std::stop_token) {
            sink(ChatEvent{ErrorEvent{.text = "summarization unavailable"}});
          }};
}

}  // namespace

TEST_CASE("MaybeAutoCompactHistory summarize replaces dropped slice") {
  auto history = MakeAlternatingHistory(15);
  std::mutex history_mutex;
  auto call_count = std::make_shared<int>(0);
  auto provider = MakeSummaryProvider("rolled-up summary", call_count);
  std::vector<ChatEvent> emitted;
  auto emit = [&emitted](ChatEvent event) {
    emitted.push_back(std::move(event));
  };
  const auto config = MakeConfig("summarize", 5);

  const auto outcome = MaybeAutoCompactHistory(
      history, history_mutex, config, provider, emit, std::stop_token{});

  REQUIRE(outcome.performed);
  REQUIRE(outcome.messages_removed == 10);
  REQUIRE(*call_count == 1);
  REQUIRE(history.size() == 6);
  REQUIRE(history.front().role == ChatRole::System);
  REQUIRE(history.front().content.find("[Earlier conversation summary]") !=
          std::string::npos);
  REQUIRE(history.front().content.find("rolled-up summary") !=
          std::string::npos);
  REQUIRE(history.back().content == "message-14");
  REQUIRE(emitted.size() == 1);
  REQUIRE(emitted[0].Type() == ChatEventType::ConversationCompacted);
}

TEST_CASE(
    "MaybeAutoCompactHistory summarize falls back to truncate on provider "
    "error") {
  auto history = MakeAlternatingHistory(12);
  std::mutex history_mutex;
  auto provider = MakeErrorProvider();
  std::vector<ChatEvent> emitted;
  auto emit = [&emitted](ChatEvent event) {
    emitted.push_back(std::move(event));
  };
  const auto config = MakeConfig("summarize", 4);

  const auto outcome = MaybeAutoCompactHistory(
      history, history_mutex, config, provider, emit, std::stop_token{});

  REQUIRE(outcome.performed);
  REQUIRE_FALSE(outcome.failure_reason.empty());
  REQUIRE(outcome.messages_removed == 8);
  REQUIRE(history.size() == 5);
  REQUIRE(history.front().role == ChatRole::System);
  REQUIRE(history.front().content.find("messages removed") !=
          std::string::npos);
  // Even with summary failure, the synthetic note is the truncate-style one
  // (no "[Earlier conversation summary]" prefix).
  REQUIRE(history.front().content.find("[Earlier conversation summary]") ==
          std::string::npos);
  REQUIRE(emitted.size() == 1);
  REQUIRE(emitted[0].Type() == ChatEventType::ConversationCompacted);
}

TEST_CASE("MaybeAutoCompactHistory summarize is a no-op below keep_last") {
  auto history = MakeAlternatingHistory(3);
  std::mutex history_mutex;
  auto call_count = std::make_shared<int>(0);
  auto provider = MakeSummaryProvider("never used", call_count);
  std::vector<ChatEvent> emitted;
  auto emit = [&emitted](ChatEvent event) {
    emitted.push_back(std::move(event));
  };
  const auto config = MakeConfig("summarize", 10);

  const auto outcome = MaybeAutoCompactHistory(
      history, history_mutex, config, provider, emit, std::stop_token{});

  REQUIRE_FALSE(outcome.performed);
  REQUIRE(*call_count == 0);
  REQUIRE(history.size() == 3);
  REQUIRE(emitted.empty());
}
