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

TEST_CASE("ParseModelsData extracts context window from /models response") {
  SECTION("OpenRouter shape: top-level context_length wins") {
    const auto models = openai_compatible_protocol::ParseModelsData(
        R"JSON({"data":[{"id":"anthropic/claude-3.5-sonnet","context_length":200000,"top_provider":{"context_length":150000}}]})JSON");
    REQUIRE(models.size() == 1);
    REQUIRE(models[0].id == "anthropic/claude-3.5-sonnet");
    REQUIRE(models[0].context_window == 200000);
  }

  SECTION("Anthropic shape: max_input_tokens") {
    const auto models = openai_compatible_protocol::ParseModelsData(
        R"JSON({"data":[{"id":"claude-opus-4","max_input_tokens":200000,"max_tokens":8192}]})JSON");
    REQUIRE(models.size() == 1);
    REQUIRE(models[0].context_window == 200000);
  }

  SECTION("Defensive aliases: max_context_length") {
    const auto models = openai_compatible_protocol::ParseModelsData(
        R"JSON({"data":[{"id":"some-model","max_context_length":65536}]})JSON");
    REQUIRE(models.size() == 1);
    REQUIRE(models[0].context_window == 65536);
  }

  SECTION("Missing field defaults to 0") {
    const auto models = openai_compatible_protocol::ParseModelsData(
        R"JSON({"data":[{"id":"gpt-4o-mini","object":"model","owned_by":"openai"}]})JSON");
    REQUIRE(models.size() == 1);
    REQUIRE(models[0].context_window == 0);
  }

  SECTION("Priority order: context_length > max_input_tokens > top_provider") {
    const auto models = openai_compatible_protocol::ParseModelsData(
        R"JSON({"data":[{"id":"m","context_length":111,"max_input_tokens":222,"max_context_length":333,"top_provider":{"context_length":444}}]})JSON");
    REQUIRE(models.size() == 1);
    REQUIRE(models[0].context_window == 111);
  }

  SECTION("top_provider fallback when top-level fields are absent") {
    const auto models = openai_compatible_protocol::ParseModelsData(
        R"JSON({"data":[{"id":"m","top_provider":{"context_length":98765}}]})JSON");
    REQUIRE(models.size() == 1);
    REQUIRE(models[0].context_window == 98765);
  }

  SECTION("String-only model entries (legacy shape) have window 0") {
    const auto models = openai_compatible_protocol::ParseModelsData(
        R"JSON({"data":["bare-model-id"]})JSON");
    REQUIRE(models.size() == 1);
    REQUIRE(models[0].id == "bare-model-id");
    REQUIRE(models[0].context_window == 0);
  }
}

TEST_CASE(
    "ConsumeSseChunk flushes pending tool calls on finish_reason: \"stop\"") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_compatible_protocol::StreamState state;
  state.sink = &sink;

  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call-stop","function":{"name":"file_read","arguments":"{\"filep"}}]}}]}
)JSON",
      state);
  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"function":{"arguments":"ath\":\"foo.txt\"}"}}]}}]}
)JSON",
      state);
  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{},"finish_reason":"stop"}]}
)JSON",
      state);

  REQUIRE(events.size() == 3);
  REQUIRE(events[0].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE(events[1].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE(events[2].Type() == ChatEventType::ToolCallRequested);

  const auto& requested = events[2].Get<ToolCallRequestedEvent>();
  REQUIRE(requested.tool_calls.size() == 1);
  REQUIRE(requested.tool_calls[0].id == "call-stop");
  REQUIRE(requested.tool_calls[0].name == "file_read");
  REQUIRE(requested.tool_calls[0].arguments_json ==
          R"({"filepath":"foo.txt"})");
  REQUIRE(state.pending_tool_calls.empty());
}

TEST_CASE(
    "ConsumeSseChunk flushes pending tool calls on finish_reason: \"length\"") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_compatible_protocol::StreamState state;
  state.sink = &sink;

  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call-len","function":{"name":"grep","arguments":"{\"pattern\":\"x\"}"}}]}}]}
)JSON",
      state);
  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{},"finish_reason":"length"}]}
)JSON",
      state);

  REQUIRE(events.size() == 2);
  REQUIRE(events[0].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE(events[1].Type() == ChatEventType::ToolCallRequested);

  const auto& requested = events[1].Get<ToolCallRequestedEvent>();
  REQUIRE(requested.tool_calls.size() == 1);
  REQUIRE(requested.tool_calls[0].id == "call-len");
  REQUIRE(requested.tool_calls[0].name == "grep");
  REQUIRE(state.pending_tool_calls.empty());
}

TEST_CASE(
    "FlushPendingToolCalls backstops streams that close without a "
    "finish_reason chunk") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_compatible_protocol::StreamState state;
  state.sink = &sink;

  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call-no-fin","function":{"name":"list_dir","arguments":"{\"path\":\"src\"}"}}]}}]}
)JSON",
      state);

  REQUIRE(events.size() == 1);
  REQUIRE(events[0].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE_FALSE(state.pending_tool_calls.empty());

  openai_compatible_protocol::FlushPendingToolCalls(state, sink);

  REQUIRE(events.size() == 2);
  REQUIRE(events[1].Type() == ChatEventType::ToolCallRequested);
  const auto& requested = events[1].Get<ToolCallRequestedEvent>();
  REQUIRE(requested.tool_calls.size() == 1);
  REQUIRE(requested.tool_calls[0].id == "call-no-fin");
  REQUIRE(requested.tool_calls[0].name == "list_dir");
  REQUIRE(requested.tool_calls[0].arguments_json == R"({"path":"src"})");
  REQUIRE(state.pending_tool_calls.empty());
}

TEST_CASE(
    "ConsumeSseChunk does not emit ToolCallRequested for a text-only stream") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_compatible_protocol::StreamState state;
  state.sink = &sink;

  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"content":"hello"},"finish_reason":null}]}
)JSON",
      state);
  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{},"finish_reason":"stop"}]}
)JSON",
      state);

  for (const auto& event : events) {
    REQUIRE(event.Type() != ChatEventType::ToolCallRequested);
  }
  REQUIRE(state.pending_tool_calls.empty());
}

TEST_CASE("FlushPendingToolCalls is a no-op when nothing is pending") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_compatible_protocol::StreamState state;
  state.sink = &sink;

  openai_compatible_protocol::FlushPendingToolCalls(state, sink);

  REQUIRE(events.empty());
  REQUIRE(state.pending_tool_calls.empty());
}

TEST_CASE(
    "ConsumeSseChunk emits argument delta before flushing on a combined "
    "chunk") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_compatible_protocol::StreamState state;
  state.sink = &sink;

  // Single chunk where choices[0] carries both the tool_call delta and the
  // terminating finish_reason. Argument-delta event must precede the
  // requested event so consumers that key off card ids see the final
  // arguments before being told the call is ready to dispatch.
  openai_compatible_protocol::ConsumeSseChunk(
      R"JSON(data: {"choices":[{"delta":{"tool_calls":[{"index":0,"id":"call-G","function":{"name":"glob","arguments":"{\"pattern\":\"*.cpp\"}"}}]},"finish_reason":"tool_calls"}]}
)JSON",
      state);

  REQUIRE(events.size() == 2);
  REQUIRE(events[0].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE(events[1].Type() == ChatEventType::ToolCallRequested);

  const auto& requested = events[1].Get<ToolCallRequestedEvent>();
  REQUIRE(requested.tool_calls.size() == 1);
  REQUIRE(requested.tool_calls[0].id == "call-G");
  REQUIRE(requested.tool_calls[0].name == "glob");
  REQUIRE(requested.tool_calls[0].arguments_json == R"({"pattern":"*.cpp"})");
  REQUIRE(state.pending_tool_calls.empty());
}
