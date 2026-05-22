#include "openai_auth_test_helpers.hpp"
#include "provider/openai_compatible_chat_provider.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using yac::tests::openai_auth::AssertHeaderAbsent;
using yac::tests::openai_auth::HttpRequest;
using yac::tests::openai_auth::HttpResponse;
using yac::tests::openai_auth::TestHttpServer;

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

TEST_CASE("OpenAiCompatibleChatProvider omits Codex OAuth affinity headers") {
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    REQUIRE(request.path == "/chat/completions");
    REQUIRE(request.headers.at("Authorization") == "Bearer test-key");
    REQUIRE_NOTHROW(AssertHeaderAbsent(request, "originator"));
    REQUIRE_NOTHROW(AssertHeaderAbsent(request, "session_id"));
    REQUIRE_NOTHROW(AssertHeaderAbsent(request, "ChatGPT-Account-Id"));
    return HttpResponse{
        .headers = {{"Content-Type", "text/event-stream"}},
        .body = "data: {\"choices\":[{\"delta\":{\"content\":\"ok\"}}]}\n"};
  });

  ProviderConfig config;
  config.id = ::yac::ProviderId{"openai-compatible"};
  config.base_url = server.Url("");
  config.api_key = "test-key";
  config.api_key_env = "UNSET_OPENAI_COMPATIBLE_TEST_KEY";
  OpenAiCompatibleChatProvider provider(config);

  ChatRequest request;
  request.model = ::yac::ModelId{"gpt-4.1"};
  request.session_id = "00000000-0000-4000-8000-000000000001";
  request.messages = {ChatMessage{.role = ChatRole::User, .content = "hello"}};

  std::vector<ChatEvent> events;
  provider.CompleteStream(
      request,
      [&events](ChatEvent event) { events.push_back(std::move(event)); }, {});

  REQUIRE(server.Requests().size() == 1);
  REQUIRE_FALSE(events.empty());
  REQUIRE(events[0].Type() == ChatEventType::TextDelta);
}
