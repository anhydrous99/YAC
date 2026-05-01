#include "provider/openai_compatible_chat_provider.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

TEST_CASE("OpenAiCompatibleChatProvider maps neutral roles") {
  REQUIRE(OpenAiCompatibleChatProvider::RoleToOpenAi(ChatRole::System) ==
          "system");
  REQUIRE(OpenAiCompatibleChatProvider::RoleToOpenAi(ChatRole::User) == "user");
  REQUIRE(OpenAiCompatibleChatProvider::RoleToOpenAi(ChatRole::Assistant) ==
          "assistant");
  REQUIRE(OpenAiCompatibleChatProvider::RoleToOpenAi(ChatRole::Tool) == "tool");
}

TEST_CASE(
    "OpenAiCompatibleChatProvider parses usage block from buffered response") {
  const auto usage = OpenAiCompatibleChatProvider::ParseUsageJson(
      R"({"usage":{"prompt_tokens":10,"completion_tokens":20,"total_tokens":30}})");

  REQUIRE(usage.has_value());
  const auto value = usage.value_or(TokenUsage{});
  REQUIRE(value.prompt_tokens == 10);
  REQUIRE(value.completion_tokens == 20);
  REQUIRE(value.total_tokens == 30);
}

TEST_CASE("OpenAiCompatibleChatProvider derives total_tokens when missing") {
  const auto usage = OpenAiCompatibleChatProvider::ParseUsageJson(
      R"({"prompt_tokens":40,"completion_tokens":60})");

  REQUIRE(usage.has_value());
  const auto value = usage.value_or(TokenUsage{});
  REQUIRE(value.total_tokens == 100);
}

TEST_CASE("OpenAiCompatibleChatProvider returns nullopt when usage is absent") {
  REQUIRE_FALSE(
      OpenAiCompatibleChatProvider::ParseUsageJson(R"({"choices":[]})"));
  REQUIRE_FALSE(OpenAiCompatibleChatProvider::ParseUsageJson("{"));
}

TEST_CASE("OpenAiCompatibleChatProvider parses streaming content delta") {
  const auto event = OpenAiCompatibleChatProvider::ParseStreamData(
      R"({"choices":[{"delta":{"content":"hello"}}]})");

  REQUIRE(event.Type() == ChatEventType::TextDelta);
  REQUIRE(event.Get<TextDeltaEvent>().text == "hello");
}

TEST_CASE(
    "OpenAiCompatibleChatProvider treats non-content chunks as empty deltas") {
  const auto event = OpenAiCompatibleChatProvider::ParseStreamData(
      R"({"choices":[{"delta":{"role":"assistant"}}]})");

  REQUIRE(event.Type() == ChatEventType::TextDelta);
  REQUIRE(event.Get<TextDeltaEvent>().text.empty());
}

TEST_CASE(
    "OpenAiCompatibleChatProvider treats reasoning chunks as empty deltas") {
  const auto event = OpenAiCompatibleChatProvider::ParseStreamData(
      R"({"choices":[{"delta":{"reasoning_content":"thinking"}}]})");

  REQUIRE(event.Type() == ChatEventType::TextDelta);
  REQUIRE(event.Get<TextDeltaEvent>().text.empty());
}

TEST_CASE(
    "OpenAiCompatibleChatProvider returns error event for malformed JSON") {
  const auto event = OpenAiCompatibleChatProvider::ParseStreamData("{");

  REQUIRE(event.Type() == ChatEventType::Error);
  REQUIRE_FALSE(event.Get<ErrorEvent>().text.empty());
}

TEST_CASE("OpenAiCompatibleChatProvider parses OpenAI-compatible model list") {
  const auto models = OpenAiCompatibleChatProvider::ParseModelsData(
      R"({"object":"list","data":[{"id":"glm-5.1"},{"id":"glm-4.7"},{"id":"glm-5.1"}]})");

  REQUIRE(models.size() == 2);
  REQUIRE(models[0].id == "glm-5.1");
  REQUIRE(models[0].display_name == "glm-5.1");
  REQUIRE(models[1].id == "glm-4.7");
}

TEST_CASE(
    "OpenAiCompatibleChatProvider returns no models for malformed model list "
    "JSON") {
  const auto models = OpenAiCompatibleChatProvider::ParseModelsData("{");

  REQUIRE(models.empty());
}
