#include "provider/openai_responses_protocol.hpp"
#include "provider/reasoning_effort_capability.hpp"

#include <exception>
#include <string>
#include <string_view>
#include <utility>

namespace yac::provider::openai_responses_protocol {

namespace {

Json ParseSchema(const std::string& schema_json) {
  try {
    return Json::parse(schema_json);
  } catch (const std::exception&) {
    return Json::object();
  }
}

std::optional<chat::TokenUsage> ExtractUsageFromNode(const Json& node) {
  if (!node.is_object()) {
    return std::nullopt;
  }

  chat::TokenUsage usage;
  if (node.contains("input_tokens") && node["input_tokens"].is_number()) {
    usage.prompt_tokens = node["input_tokens"].get<int>();
  }
  if (node.contains("output_tokens") && node["output_tokens"].is_number()) {
    usage.completion_tokens = node["output_tokens"].get<int>();
  }
  if (node.contains("total_tokens") && node["total_tokens"].is_number()) {
    usage.total_tokens = node["total_tokens"].get<int>();
  }

  if (usage.prompt_tokens == 0 && usage.completion_tokens == 0 &&
      usage.total_tokens == 0) {
    return std::nullopt;
  }
  if (usage.total_tokens == 0) {
    usage.total_tokens = usage.prompt_tokens + usage.completion_tokens;
  }
  if (usage.prompt_tokens == 0 && usage.total_tokens > 0) {
    usage.prompt_tokens = usage.total_tokens - usage.completion_tokens;
  }
  return usage;
}

std::string ErrorTextFromNode(const Json& json) {
  if (json.contains("message") && json["message"].is_string()) {
    return json["message"].get<std::string>();
  }
  if (json.contains("error")) {
    const auto& error = json["error"];
    if (error.is_object() && error.contains("message") &&
        error["message"].is_string()) {
      return error["message"].get<std::string>();
    }
    return error.dump();
  }
  if (json.contains("response") && json["response"].is_object()) {
    const auto& response = json["response"];
    if (response.contains("error")) {
      const auto& error = response["error"];
      if (error.is_object() && error.contains("message") &&
          error["message"].is_string()) {
        return error["message"].get<std::string>();
      }
      return error.dump();
    }
    if (response.contains("incomplete_details") &&
        response["incomplete_details"].is_object()) {
      return response["incomplete_details"].dump();
    }
  }
  return json.dump();
}

int ToolCallIndexFromEvent(const Json& event) {
  if (event.contains("output_index") && event["output_index"].is_number()) {
    return event["output_index"].get<int>();
  }
  return -1;
}

int AccumulateOutputItem(const Json& item, const Json& event,
                         StreamState& state) {
  const auto index = ToolCallIndexFromEvent(event);
  if (index < 0) {
    return -1;
  }

  auto& pending = state.pending_tool_calls[index];
  if (event.contains("item_id") && event["item_id"].is_string()) {
    pending.item_id = event["item_id"].get<std::string>();
  }
  if (item.contains("id") && item["id"].is_string()) {
    pending.item_id = item["id"].get<std::string>();
  }
  if (item.contains("call_id") && item["call_id"].is_string()) {
    pending.call_id = item["call_id"].get<std::string>();
  }
  if (item.contains("name") && item["name"].is_string()) {
    pending.name = item["name"].get<std::string>();
  }
  if (item.contains("arguments") && item["arguments"].is_string()) {
    pending.arguments_json = item["arguments"].get<std::string>();
  }
  return index;
}

int AccumulateArgumentDelta(const Json& event, StreamState& state) {
  const auto index = ToolCallIndexFromEvent(event);
  if (index < 0) {
    return -1;
  }

  auto& pending = state.pending_tool_calls[index];
  if (event.contains("item_id") && event["item_id"].is_string()) {
    pending.item_id = event["item_id"].get<std::string>();
  }
  if (event.contains("call_id") && event["call_id"].is_string()) {
    pending.call_id = event["call_id"].get<std::string>();
  }
  if (event.contains("name") && event["name"].is_string()) {
    pending.name = event["name"].get<std::string>();
  }
  if (event.contains("delta") && event["delta"].is_string()) {
    pending.arguments_json += event["delta"].get<std::string>();
  }
  if (event.contains("arguments") && event["arguments"].is_string()) {
    pending.arguments_json = event["arguments"].get<std::string>();
  }
  return index;
}

void EmitArgumentDeltaIfReady(int index, StreamState& state) {
  if (state.sink == nullptr || index < 0) {
    return;
  }

  const auto& pending = state.pending_tool_calls[index];
  if (pending.call_id.empty()) {
    return;
  }

  (*state.sink)(chat::ChatEvent{chat::ToolCallArgumentDeltaEvent{
      .tool_call_id = ::yac::ToolCallId{pending.call_id},
      .tool_name = pending.name,
      .arguments_json = pending.arguments_json,
  }});
}

void FlushPendingToolCall(int index, StreamState& state) {
  if (state.sink == nullptr) {
    return;
  }
  const auto it = state.pending_tool_calls.find(index);
  if (it == state.pending_tool_calls.end()) {
    return;
  }

  const auto& call = it->second;
  if (!call.call_id.empty() && !call.name.empty()) {
    (*state.sink)(chat::ChatEvent{chat::ToolCallRequestedEvent{
        .tool_calls = {
            chat::ToolCallRequest{.id = call.call_id,
                                  .name = call.name,
                                  .arguments_json = call.arguments_json}}}});
  }
  state.pending_tool_calls.erase(it);
}

void FlushUsageFromResponse(const Json& json, StreamState& state) {
  if (!json.contains("response") || !json["response"].is_object()) {
    return;
  }
  const auto& response = json["response"];
  if (!response.contains("usage")) {
    return;
  }
  if (auto usage = ExtractUsageFromNode(response["usage"])) {
    state.pending_usage = std::move(usage);
  }
}

void DispatchSseData(const std::string& data, StreamState& state) {
  try {
    const auto json = Json::parse(data);
    const auto type = json.value("type", std::string{});

    if (type == "error") {
      (*state.sink)(
          chat::ChatEvent{chat::ErrorEvent{.text = ErrorTextFromNode(json)}});
      return;
    }

    if (type == "response.output_text.delta") {
      if (json.contains("delta") && json["delta"].is_string()) {
        const auto text = json["delta"].get<std::string>();
        if (!text.empty()) {
          (*state.sink)(
              chat::ChatEvent{chat::TextDeltaEvent{.text = std::move(text)}});
        }
      }
      return;
    }

    if (type == "response.output_item.added" ||
        type == "response.output_item.done") {
      if (!json.contains("item") || !json["item"].is_object()) {
        return;
      }
      const auto& item = json["item"];
      if (!item.contains("type") || !item["type"].is_string() ||
          item["type"].get<std::string>() != "function_call") {
        return;
      }
      const auto index = AccumulateOutputItem(item, json, state);
      if (type == "response.output_item.done") {
        EmitArgumentDeltaIfReady(index, state);
      }
      return;
    }

    if (type == "response.function_call_arguments.delta") {
      const auto index = AccumulateArgumentDelta(json, state);
      EmitArgumentDeltaIfReady(index, state);
      return;
    }

    if (type == "response.function_call_arguments.done") {
      const auto index = AccumulateArgumentDelta(json, state);
      EmitArgumentDeltaIfReady(index, state);
      FlushPendingToolCall(index, state);
      return;
    }

    if (type == "response.completed" || type == "response.incomplete") {
      FlushUsageFromResponse(json, state);
      FlushPendingToolCalls(state, *state.sink);
      return;
    }

    if (type == "response.failed") {
      FlushUsageFromResponse(json, state);
      FlushPendingToolCalls(state, *state.sink);
      (*state.sink)(
          chat::ChatEvent{chat::ErrorEvent{.text = ErrorTextFromNode(json)}});
      return;
    }

    if (type == "response.output_text.done") {
      return;
    }
  } catch (const std::exception& error) {
    (*state.sink)(chat::ChatEvent{chat::ErrorEvent{.text = error.what()}});
  }
}

void ConsumeSseLine(std::string_view line, StreamState& state) {
  constexpr std::string_view kPrefix = "data: ";
  if (!line.starts_with(kPrefix)) {
    return;
  }

  const auto data = line.substr(kPrefix.size());
  if (data == "[DONE]") {
    return;
  }

  DispatchSseData(std::string(data), state);
}

}  // namespace

std::string ResponsesPath() {
  return "/backend-api/codex/responses";
}

Json BuildResponsesPayload(const chat::ChatRequest& request,
                           const chat::ProviderConfig& config) {
  (void)config;

  Json input = Json::array();
  for (const auto& message : request.messages) {
    if (message.role == chat::ChatRole::System) {
      if (request.responses_instructions.has_value()) {
        continue;
      }
      input.push_back({{"role", "system"}, {"content", message.content}});
      continue;
    }

    if (message.role == chat::ChatRole::User) {
      input.push_back(
          {{"role", "user"},
           {"content", Json::array({{{"type", "input_text"},
                                     {"text", message.content}}})}});
      continue;
    }

    if (message.role == chat::ChatRole::Assistant) {
      if (!message.content.empty()) {
        input.push_back(
            {{"role", "assistant"},
             {"content", Json::array({{{"type", "output_text"},
                                       {"text", message.content}}})}});
      }
      for (const auto& call : message.tool_calls) {
        input.push_back({{"type", "function_call"},
                         {"call_id", call.id},
                         {"name", call.name},
                         {"arguments", call.arguments_json}});
      }
      continue;
    }

    input.push_back({{"type", "function_call_output"},
                     {"call_id", message.tool_call_id.value},
                     {"output", message.content}});
  }

  Json payload{{"model", request.model.value},
               {"input", std::move(input)},
               {"stream", true},
               {"store", false}};

  if (request.responses_instructions.has_value()) {
    payload["instructions"] = *request.responses_instructions;
  }

  if (request.reasoning_effort.has_value()) {
    payload["reasoning"] =
        Json{{"effort", std::string(ToString(*request.reasoning_effort))}};
  }

  if (!request.tools.empty()) {
    Json tools = Json::array();
    for (const auto& tool : request.tools) {
      tools.push_back(
          {{"type", "function"},
           {"name", tool.name},
           {"description", tool.description},
           {"parameters", ParseSchema(tool.parameters_schema_json)}});
    }
    payload["tools"] = std::move(tools);
    payload["tool_choice"] = "auto";
  }

  return payload;
}

chat::ChatEvent ParseStreamData(const std::string& data) {
  try {
    const auto json = Json::parse(data);
    const auto type = json.value("type", std::string{});
    if (type == "error" || type == "response.failed") {
      return chat::ChatEvent{chat::ErrorEvent{.text = ErrorTextFromNode(json)}};
    }
    if (type == "response.output_text.delta" && json.contains("delta") &&
        json["delta"].is_string()) {
      return chat::ChatEvent{
          chat::TextDeltaEvent{.text = json["delta"].get<std::string>()}};
    }
    return chat::ChatEvent{chat::TextDeltaEvent{}};
  } catch (const std::exception& error) {
    return chat::ChatEvent{chat::ErrorEvent{.text = error.what()}};
  }
}

std::optional<chat::TokenUsage> ParseUsageJson(const std::string& data) {
  try {
    const auto json = Json::parse(data);
    if (json.contains("response") && json["response"].is_object() &&
        json["response"].contains("usage")) {
      return ExtractUsageFromNode(json["response"]["usage"]);
    }
    if (json.contains("usage")) {
      return ExtractUsageFromNode(json["usage"]);
    }
    return ExtractUsageFromNode(json);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

void ConsumeSseChunk(std::string_view chunk, StreamState& state) {
  state.buffer.append(chunk.data(), chunk.size());

  size_t pos = 0;
  while ((pos = state.buffer.find('\n')) != std::string::npos) {
    auto line = state.buffer.substr(0, pos);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    state.buffer.erase(0, pos + 1);
    ConsumeSseLine(line, state);
  }
}

void FlushPendingToolCalls(StreamState& state, ChatEventSink& sink) {
  std::vector<chat::ToolCallRequest> calls;
  for (const auto& [index, call] : state.pending_tool_calls) {
    (void)index;
    if (call.call_id.empty() || call.name.empty()) {
      continue;
    }
    calls.push_back(
        chat::ToolCallRequest{.id = call.call_id,
                              .name = call.name,
                              .arguments_json = call.arguments_json});
  }
  state.pending_tool_calls.clear();
  if (!calls.empty()) {
    sink(chat::ChatEvent{
        chat::ToolCallRequestedEvent{.tool_calls = std::move(calls)}});
  }
}

}  // namespace yac::provider::openai_responses_protocol
