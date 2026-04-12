#include "provider/openai_chat_provider.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

TEST_CASE("OpenAiChatProvider maps neutral roles") {
  REQUIRE(OpenAiChatProvider::RoleToOpenAi(ChatRole::System) == "system");
  REQUIRE(OpenAiChatProvider::RoleToOpenAi(ChatRole::User) == "user");
  REQUIRE(OpenAiChatProvider::RoleToOpenAi(ChatRole::Assistant) == "assistant");
  REQUIRE(OpenAiChatProvider::RoleToOpenAi(ChatRole::Tool) == "tool");
}

TEST_CASE("OpenAiChatProvider parses streaming content delta") {
  const auto event = OpenAiChatProvider::ParseStreamData(
      R"({"choices":[{"delta":{"content":"hello"}}]})");

  REQUIRE(event.type == ChatEventType::TextDelta);
  REQUIRE(event.text == "hello");
}

TEST_CASE("OpenAiChatProvider treats non-content chunks as empty deltas") {
  const auto event = OpenAiChatProvider::ParseStreamData(
      R"({"choices":[{"delta":{"role":"assistant"}}]})");

  REQUIRE(event.type == ChatEventType::TextDelta);
  REQUIRE(event.text.empty());
}

TEST_CASE("OpenAiChatProvider returns error event for malformed JSON") {
  const auto event = OpenAiChatProvider::ParseStreamData("{");

  REQUIRE(event.type == ChatEventType::Error);
  REQUIRE_FALSE(event.text.empty());
}
