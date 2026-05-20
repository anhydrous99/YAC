#include "provider/openai_responses_protocol.hpp"

#include <catch2/catch_approx.hpp>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

TEST_CASE("BuildResponsesPayload lowers messages and tools for Codex OAuth") {
  ChatRequest request;
  request.model = ::yac::ModelId{"gpt-5.4"};
  request.temperature = 0.2;
  request.messages = {
      ChatMessage{.role = ChatRole::System, .content = "system prompt"},
      ChatMessage{.role = ChatRole::User, .content = "hello"},
      ChatMessage{.role = ChatRole::Assistant,
                  .content = "I will call a tool",
                  .tool_calls = {ToolCallRequest{.id = "call-1",
                                                 .name = "file_read",
                                                 .arguments_json =
                                                     R"({"path":"README.md"})"}}},
      ChatMessage{.role = ChatRole::Tool,
                  .content = R"({"contents":"ok"})",
                  .tool_call_id = ::yac::ToolCallId{"call-1"},
                  .tool_name = "file_read"},
  };
  request.tools = {ToolDefinition{.name = "file_read",
                                  .description = "Read a file",
                                  .parameters_schema_json =
                                      R"({"type":"object","properties":{"path":{"type":"string"}}})"}};

  ProviderConfig config;
  const auto payload =
      openai_responses_protocol::BuildResponsesPayload(request, config);

  REQUIRE(openai_responses_protocol::ResponsesPath() ==
          "/backend-api/codex/responses");
  REQUIRE(payload["model"].get<std::string>() == "gpt-5.4");
  REQUIRE(payload["stream"].get<bool>());
  REQUIRE_FALSE(payload["store"].get<bool>());
  REQUIRE(payload["temperature"].get<double>() == Catch::Approx(0.2));
  REQUIRE(payload["input"].size() == 5);
  REQUIRE(payload["input"][0]["role"].get<std::string>() == "system");
  REQUIRE(payload["input"][0]["content"].get<std::string>() ==
          "system prompt");
  REQUIRE(payload["input"][1]["role"].get<std::string>() == "user");
  REQUIRE(payload["input"][1]["content"][0]["type"].get<std::string>() ==
          "input_text");
  REQUIRE(payload["input"][1]["content"][0]["text"].get<std::string>() ==
          "hello");
  REQUIRE(payload["input"][2]["role"].get<std::string>() == "assistant");
  REQUIRE(payload["input"][2]["content"][0]["type"].get<std::string>() ==
          "output_text");
  REQUIRE(payload["input"][2]["content"][0]["text"].get<std::string>() ==
          "I will call a tool");
  REQUIRE(payload["input"][3]["type"].get<std::string>() ==
          "function_call");
  REQUIRE(payload["input"][3]["call_id"].get<std::string>() == "call-1");
  REQUIRE(payload["input"][3]["name"].get<std::string>() == "file_read");
  REQUIRE(payload["input"][3]["arguments"].get<std::string>() ==
          R"({"path":"README.md"})");
  REQUIRE(payload["input"][4]["type"].get<std::string>() ==
          "function_call_output");
  REQUIRE(payload["input"][4]["call_id"].get<std::string>() == "call-1");
  REQUIRE(payload["input"][4]["output"].get<std::string>() ==
          R"({"contents":"ok"})");
  REQUIRE(payload["tool_choice"].get<std::string>() == "auto");
  REQUIRE(payload["tools"][0]["type"].get<std::string>() == "function");
  REQUIRE(payload["tools"][0]["name"].get<std::string>() == "file_read");
  REQUIRE(payload["tools"][0]["parameters"]["type"].get<std::string>() ==
          "object");
}

TEST_CASE(
    "BuildResponsesPayload tolerates invalid tool schema like chat protocol") {
  ChatRequest request;
  request.model = ::yac::ModelId{"gpt-5.4"};
  request.tools = {ToolDefinition{.name = "bad",
                                  .description = "broken",
                                  .parameters_schema_json = "{"}};

  ProviderConfig config;
  const auto payload =
      openai_responses_protocol::BuildResponsesPayload(request, config);

  REQUIRE(payload["tools"][0]["parameters"].is_object());
  REQUIRE(payload["tools"][0]["parameters"].empty());
}

TEST_CASE("ParseUsageJson maps Responses usage fields") {
  const auto usage = openai_responses_protocol::ParseUsageJson(
      R"JSON({"response":{"usage":{"input_tokens":7,"output_tokens":5}}})JSON");

  REQUIRE(usage.has_value());
  REQUIRE(usage->prompt_tokens == 7);
  REQUIRE(usage->completion_tokens == 5);
  REQUIRE(usage->total_tokens == 12);
}

TEST_CASE("ParseUsageJson returns nullopt for absent or malformed usage") {
  REQUIRE_FALSE(openai_responses_protocol::ParseUsageJson(
      R"JSON({"response":{"usage":null}})JSON"));
  REQUIRE_FALSE(openai_responses_protocol::ParseUsageJson("{"));
}

TEST_CASE(
    "ParseStreamData handles text deltas, failure, and malformed JSON") {
  const auto delta = openai_responses_protocol::ParseStreamData(
      R"JSON({"type":"response.output_text.delta","delta":"hello"})JSON");
  REQUIRE(delta.Type() == ChatEventType::TextDelta);
  REQUIRE(delta.Get<TextDeltaEvent>().text == "hello");

  const auto failure = openai_responses_protocol::ParseStreamData(
      R"JSON({"type":"response.failed","response":{"error":{"message":"boom"}}})JSON");
  REQUIRE(failure.Type() == ChatEventType::Error);
  REQUIRE(failure.Get<ErrorEvent>().text == "boom");

  const auto malformed = openai_responses_protocol::ParseStreamData("{");
  REQUIRE(malformed.Type() == ChatEventType::Error);
  REQUIRE_FALSE(malformed.Get<ErrorEvent>().text.empty());
}

TEST_CASE(
    "ConsumeSseChunk handles text, tool deltas, done, usage, and completion") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_responses_protocol::StreamState state;
  state.sink = &sink;

  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.output_text.delta","delta":"Hel)JSON",
      state);
  REQUIRE(events.empty());

  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(lo"}
)JSON",
      state);
  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.output_text.done","text":"Hello"}
)JSON",
      state);
  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.output_item.added","output_index":0,"item":{"id":"fc_1","type":"function_call","call_id":"call-1","name":"grep"}}
)JSON",
      state);
  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.function_call_arguments.delta","output_index":0,"item_id":"fc_1","call_id":"call-1","delta":"{\"pattern\":\"hel"}
)JSON",
      state);
  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.function_call_arguments.done","output_index":0,"item_id":"fc_1","call_id":"call-1","name":"grep","arguments":"{\"pattern\":\"hello\"}"}
)JSON",
      state);
  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.completed","response":{"usage":{"input_tokens":3,"output_tokens":4,"total_tokens":7}}}
)JSON",
      state);

  REQUIRE(events.size() == 4);
  REQUIRE(events[0].Type() == ChatEventType::TextDelta);
  REQUIRE(events[0].Get<TextDeltaEvent>().text == "Hello");
  REQUIRE(events[1].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE(events[1].Get<ToolCallArgumentDeltaEvent>().tool_call_id ==
          ::yac::ToolCallId{"call-1"});
  REQUIRE(events[1].Get<ToolCallArgumentDeltaEvent>().tool_name == "grep");
  REQUIRE(events[1].Get<ToolCallArgumentDeltaEvent>().arguments_json ==
          std::string{"{\"pattern\":\"hel"});
  REQUIRE(events[2].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE(events[2].Get<ToolCallArgumentDeltaEvent>().arguments_json ==
          R"({"pattern":"hello"})");
  REQUIRE(events[3].Type() == ChatEventType::ToolCallRequested);
  REQUIRE(events[3].Get<ToolCallRequestedEvent>().tool_calls.size() == 1);
  REQUIRE(events[3].Get<ToolCallRequestedEvent>().tool_calls[0].id ==
          "call-1");
  REQUIRE(events[3].Get<ToolCallRequestedEvent>().tool_calls[0].name ==
          "grep");
  REQUIRE(
      events[3].Get<ToolCallRequestedEvent>().tool_calls[0].arguments_json ==
      R"({"pattern":"hello"})");
  REQUIRE(state.pending_usage.has_value());
  REQUIRE(state.pending_usage->prompt_tokens == 3);
  REQUIRE(state.pending_usage->completion_tokens == 4);
  REQUIRE(state.pending_usage->total_tokens == 7);
}

TEST_CASE("ConsumeSseChunk surfaces mid-stream error and malformed JSON") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_responses_protocol::StreamState state;
  state.sink = &sink;

  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"error","message":"temporary upstream issue"}
)JSON",
      state);
  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {
)JSON",
      state);

  REQUIRE(events.size() == 2);
  REQUIRE(events[0].Type() == ChatEventType::Error);
  REQUIRE(events[0].Get<ErrorEvent>().text == "temporary upstream issue");
  REQUIRE(events[1].Type() == ChatEventType::Error);
  REQUIRE_FALSE(events[1].Get<ErrorEvent>().text.empty());
}

TEST_CASE("ConsumeSseChunk flushes pending tool call on response.failed") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_responses_protocol::StreamState state;
  state.sink = &sink;

  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.output_item.added","output_index":0,"item":{"id":"fc_fail","type":"function_call","call_id":"call-fail","name":"list_dir"}}
)JSON",
      state);
  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.function_call_arguments.delta","output_index":0,"item_id":"fc_fail","call_id":"call-fail","delta":"{\"path\":\"src\"}"}
)JSON",
      state);
  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.failed","response":{"error":{"message":"request failed"},"usage":null}}
)JSON",
      state);

  REQUIRE(events.size() == 3);
  REQUIRE(events[0].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE(events[1].Type() == ChatEventType::ToolCallRequested);
  REQUIRE(events[1].Get<ToolCallRequestedEvent>().tool_calls[0].id ==
          "call-fail");
  REQUIRE(
      events[1].Get<ToolCallRequestedEvent>().tool_calls[0].arguments_json ==
      R"({"path":"src"})");
  REQUIRE(events[2].Type() == ChatEventType::Error);
  REQUIRE(events[2].Get<ErrorEvent>().text == "request failed");
  REQUIRE(state.pending_tool_calls.empty());
  REQUIRE_FALSE(state.pending_usage.has_value());
}

TEST_CASE("FlushPendingToolCalls backstops completed streams without done") {
  std::vector<ChatEvent> events;
  ChatEventSink sink = [&events](ChatEvent event) {
    events.push_back(std::move(event));
  };

  openai_responses_protocol::StreamState state;
  state.sink = &sink;

  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.output_item.added","output_index":2,"item":{"id":"fc_backstop","type":"function_call","call_id":"call-backstop","name":"glob"}}
)JSON",
      state);
  openai_responses_protocol::ConsumeSseChunk(
      R"JSON(data: {"type":"response.function_call_arguments.delta","output_index":2,"item_id":"fc_backstop","call_id":"call-backstop","delta":"{\"pattern\":\"*.cpp\"}"}
)JSON",
      state);

  REQUIRE(events.size() == 1);
  REQUIRE(events[0].Type() == ChatEventType::ToolCallArgumentDelta);
  REQUIRE_FALSE(state.pending_tool_calls.empty());

  openai_responses_protocol::FlushPendingToolCalls(state, sink);

  REQUIRE(events.size() == 2);
  REQUIRE(events[1].Type() == ChatEventType::ToolCallRequested);
  REQUIRE(events[1].Get<ToolCallRequestedEvent>().tool_calls[0].id ==
          "call-backstop");
  REQUIRE(events[1].Get<ToolCallRequestedEvent>().tool_calls[0].name ==
          "glob");
  REQUIRE(
      events[1].Get<ToolCallRequestedEvent>().tool_calls[0].arguments_json ==
      R"({"pattern":"*.cpp"})");
  REQUIRE(state.pending_tool_calls.empty());
}
