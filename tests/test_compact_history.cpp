#include "chat/chat_service_history.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::chat::internal;

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

}  // namespace

TEST_CASE("CompactHistory") {
  SECTION("No-op when history has fewer messages than keep_last") {
    auto history = MakeAlternatingHistory(5);

    CompactHistory(history, 10);

    REQUIRE(history.size() == 5);
    for (std::size_t i = 0; i < history.size(); ++i) {
      REQUIRE(history[i].role ==
              ((i % 2 == 0) ? ChatRole::User : ChatRole::Assistant));
      REQUIRE(history[i].status == ChatMessageStatus::Complete);
      REQUIRE(history[i].content == "message-" + std::to_string(i));
    }
  }

  SECTION("Removes oldest messages keeping last N") {
    auto history = MakeAlternatingHistory(15);

    CompactHistory(history, 5);

    REQUIRE(history.size() == 6);
    REQUIRE(history.front().role == ChatRole::System);
    REQUIRE(history.front().status == ChatMessageStatus::Complete);
    REQUIRE(history.front().content ==
            "[Earlier conversation compacted. 10 messages removed.]");

    for (std::size_t i = 0; i < 5; ++i) {
      const auto& message = history[i + 1];
      REQUIRE(message.role ==
              (((10 + i) % 2 == 0) ? ChatRole::User : ChatRole::Assistant));
      REQUIRE(message.content == "message-" + std::to_string(10 + i));
    }
  }

  SECTION("Synthetic message has correct removed count") {
    auto history = MakeAlternatingHistory(15);

    CompactHistory(history, 5);

    REQUIRE(history.front().content.find("10 messages removed") !=
            std::string::npos);
  }

  SECTION("Preserves leading system messages") {
    std::vector<ChatMessage> history;
    history.push_back(MakeMessage(ChatRole::System, "system"));
    for (std::size_t i = 0; i < 12; ++i) {
      const auto role = (i % 2 == 0) ? ChatRole::User : ChatRole::Assistant;
      history.push_back(MakeMessage(role, "message-" + std::to_string(i)));
    }

    CompactHistory(history, 5);

    REQUIRE(history.size() == 7);
    REQUIRE(history[0].role == ChatRole::System);
    REQUIRE(history[0].content == "system");
    REQUIRE(history[1].role == ChatRole::System);
    REQUIRE(history[1].content ==
            "[Earlier conversation compacted. 7 messages removed.]");
  }

  SECTION("Exact boundary: keep_last equals history size") {
    auto history = MakeAlternatingHistory(10);

    CompactHistory(history, 10);

    REQUIRE(history.size() == 10);
    REQUIRE(history.front().role != ChatRole::System);
    REQUIRE(history.front().content == "message-0");
    REQUIRE(history.back().content == "message-9");
  }
}
