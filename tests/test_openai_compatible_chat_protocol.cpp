#include "provider/openai_compatible_chat_protocol.hpp"

#include <atomic>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

TEST_CASE("BuildChatPayload preserves tool call and tool result metadata") {
  ChatRequest request;
  request.model = "gpt-4.1";
  request.temperature = 0.3;
  request.messages = {
      ChatMessage{.role = ChatRole::System, .content = "system prompt"},
      ChatMessage{.role = ChatRole::Assistant,
                  .content = "",
                  .tool_calls = {ToolCallRequest{
                      .id = "call-1",
                      .name = "list_dir",
                      .arguments_json = R"({"path":"src"})"}}},
      ChatMessage{.role = ChatRole::Tool,
                  .content = "src/main.cpp",
                  .tool_call_id = "call-1",
                  .tool_name = "list_dir"},
  };
  request.tools = {ToolDefinition{
      .name = "list_dir",
      .description = "List workspace files",
      .parameters_schema_json =
          R"({"type":"object","properties":{"path":{"type":"string"}}})"}};

  ProviderConfig config;
  config.options["include_stream_usage"] = "false";

  const auto payload =
      openai_compatible_protocol::BuildChatPayload(request, true, config);

  REQUIRE(payload["model"].get<std::string>() == "gpt-4.1");
  REQUIRE(payload["stream"].get<bool>());
  REQUIRE_FALSE(payload.contains("stream_options"));
  REQUIRE(payload["messages"].size() == 3);
  REQUIRE(payload["messages"][1]["tool_calls"][0]["id"].get<std::string>() ==
          "call-1");
  REQUIRE(payload["messages"][1]["tool_calls"][0]["function"]["name"]
              .get<std::string>() == "list_dir");
  REQUIRE(payload["messages"][2]["tool_call_id"].get<std::string>() ==
          "call-1");
  REQUIRE(payload["messages"][2]["name"].get<std::string>() == "list_dir");
  REQUIRE(payload["tool_choice"].get<std::string>() == "auto");
  REQUIRE(payload["tools"][0]["function"]["parameters"]["type"]
              .get<std::string>() == "object");
}

TEST_CASE("Buffered response helpers extract content, usage, and tool calls") {
  const auto response = openai_compatible_protocol::Json::parse(
      R"JSON({"choices":[{"message":{"content":"final answer","tool_calls":[{"id":"call-2","function":{"name":"file_read","arguments":"{\"path\":\"README.md\"}"}}]}}],"usage":{"prompt_tokens":4,"completion_tokens":6}})JSON");

  REQUIRE(openai_compatible_protocol::ExtractBufferedText(response) ==
          "final answer");

  const auto usage = openai_compatible_protocol::ExtractBufferedUsage(response);
  REQUIRE(usage.has_value());
  REQUIRE(usage->prompt_tokens == 4);
  REQUIRE(usage->completion_tokens == 6);
  REQUIRE(usage->total_tokens == 10);

  const auto calls =
      openai_compatible_protocol::ExtractBufferedToolCalls(response);
  REQUIRE(calls.size() == 1);
  REQUIRE(calls[0].id == "call-2");
  REQUIRE(calls[0].name == "file_read");
  REQUIRE(calls[0].arguments_json == R"({"path":"README.md"})");
}

TEST_CASE(
    "ConsumeSseChunk buffers partial lines and emits accumulated tool calls") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_compatible_protocol::StreamState state;
  state.sink = &sink;

  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"content":"hel)JSON", state);
  REQUIRE(events.empty());

  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(lo"}}]}
)JSON",
      state);

  REQUIRE(events.size() == 1);
  REQUIRE(events[0].Type() == ChatEventType::TextDelta);
  REQUIRE(events[0].Get<TextDeltaEvent>().text == "hello");

  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call-3","function":{"name":"list_dir","arguments":"{\"path\":\""}}]}}]}
)JSON",
      state);
  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"usage":{"prompt_tokens":2,"completion_tokens":3},"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"src\"}"}}]}}]}
)JSON",
      state);
  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"finish_reason":"tool_calls"}]}
)JSON",
      state);

  REQUIRE(events.size() == 4);
  REQUIRE(events[1].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE(events[2].Type() == ChatEventType::ToolCallArgumentDelta);
  const auto& first_delta = events[1].Get<ToolCallArgumentDeltaEvent>();
  REQUIRE(first_delta.tool_call_id == "call-3");
  REQUIRE(first_delta.tool_name == "list_dir");
  REQUIRE(first_delta.arguments_json == R"({"path":")");
  const auto& second_delta = events[2].Get<ToolCallArgumentDeltaEvent>();
  REQUIRE(second_delta.arguments_json == R"({"path":"src"})");

  REQUIRE(events[3].Type() == ChatEventType::ToolCallRequested);
  const auto& requested = events[3].Get<ToolCallRequestedEvent>();
  REQUIRE(requested.tool_calls.size() == 1);
  REQUIRE(requested.tool_calls[0].id == "call-3");
  REQUIRE(requested.tool_calls[0].name == "list_dir");
  REQUIRE(requested.tool_calls[0].arguments_json == R"({"path":"src"})");

  REQUIRE(state.pending_usage.has_value());
  REQUIRE(state.pending_usage->prompt_tokens == 2);
  REQUIRE(state.pending_usage->completion_tokens == 3);
  REQUIRE(state.pending_usage->total_tokens == 5);
}

TEST_CASE(
    "ConsumeSseChunk suppresses argument deltas before tool id is known") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_compatible_protocol::StreamState state;
  state.sink = &sink;

  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"name":"file_write","arguments":"{\"filep"}}]}}]}
)JSON",
      state);
  REQUIRE(events.empty());

  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call-4","function":{"arguments":"ath\":\"foo.txt\",\"content\":\"hi\"}"}}]}}]}
)JSON",
      state);
  REQUIRE(events.size() == 1);
  REQUIRE(events[0].Type() == ChatEventType::ToolCallArgumentDelta);
  const auto& delta = events[0].Get<ToolCallArgumentDeltaEvent>();
  REQUIRE(delta.tool_call_id == "call-4");
  REQUIRE(delta.tool_name == "file_write");
  REQUIRE(delta.arguments_json == R"({"filepath":"foo.txt","content":"hi"})");
}
