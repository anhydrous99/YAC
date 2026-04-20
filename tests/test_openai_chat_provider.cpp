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

TEST_CASE("OpenAiChatProvider parses usage block from buffered response") {
  const auto usage = OpenAiChatProvider::ParseUsageJson(
      R"({"usage":{"prompt_tokens":10,"completion_tokens":20,"total_tokens":30}})");

  REQUIRE(usage.has_value());
  const auto value = usage.value_or(TokenUsage{});
  REQUIRE(value.prompt_tokens == 10);
  REQUIRE(value.completion_tokens == 20);
  REQUIRE(value.total_tokens == 30);
}

TEST_CASE("OpenAiChatProvider derives total_tokens when missing") {
  const auto usage = OpenAiChatProvider::ParseUsageJson(
      R"({"prompt_tokens":40,"completion_tokens":60})");

  REQUIRE(usage.has_value());
  const auto value = usage.value_or(TokenUsage{});
  REQUIRE(value.total_tokens == 100);
}

TEST_CASE("OpenAiChatProvider returns nullopt when usage is absent") {
  REQUIRE_FALSE(OpenAiChatProvider::ParseUsageJson(R"({"choices":[]})"));
  REQUIRE_FALSE(OpenAiChatProvider::ParseUsageJson("{"));
}

TEST_CASE("OpenAiChatProvider parses streaming content delta") {
  const auto event = OpenAiChatProvider::ParseStreamData(
      R"({"choices":[{"delta":{"content":"hello"}}]})");

  REQUIRE(event.Type() == ChatEventType::TextDelta);
  REQUIRE(event.Get<TextDeltaEvent>().text == "hello");
}

TEST_CASE("OpenAiChatProvider treats non-content chunks as empty deltas") {
  const auto event = OpenAiChatProvider::ParseStreamData(
      R"({"choices":[{"delta":{"role":"assistant"}}]})");

  REQUIRE(event.Type() == ChatEventType::TextDelta);
  REQUIRE(event.Get<TextDeltaEvent>().text.empty());
}

TEST_CASE("OpenAiChatProvider treats reasoning chunks as empty deltas") {
  const auto event = OpenAiChatProvider::ParseStreamData(
      R"({"choices":[{"delta":{"reasoning_content":"thinking"}}]})");

  REQUIRE(event.Type() == ChatEventType::TextDelta);
  REQUIRE(event.Get<TextDeltaEvent>().text.empty());
}

TEST_CASE("OpenAiChatProvider returns error event for malformed JSON") {
  const auto event = OpenAiChatProvider::ParseStreamData("{");

  REQUIRE(event.Type() == ChatEventType::Error);
  REQUIRE_FALSE(event.Get<ErrorEvent>().text.empty());
}

TEST_CASE("OpenAiChatProvider parses OpenAI-compatible model list") {
  const auto models = OpenAiChatProvider::ParseModelsData(
      R"({"object":"list","data":[{"id":"glm-5.1"},{"id":"glm-4.7"},{"id":"glm-5.1"}]})");

  REQUIRE(models.size() == 2);
  REQUIRE(models[0].id == "glm-5.1");
  REQUIRE(models[0].display_name == "glm-5.1");
  REQUIRE(models[1].id == "glm-4.7");
}

TEST_CASE(
    "OpenAiChatProvider returns no models for malformed model list JSON") {
  const auto models = OpenAiChatProvider::ParseModelsData("{");

  REQUIRE(models.empty());
}
