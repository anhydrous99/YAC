#include "provider/openai_compatible_chat_protocol.hpp"

#include <array>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>

namespace yac::provider::openai_compatible_protocol {

namespace {

Json ParseSchema(const std::string& schema_json) {
  try {
    return Json::parse(schema_json);
  } catch (const std::exception&) {
    return Json::object();
  }
}

bool ProviderOptionEnabled(const std::map<std::string, std::string>& options,
                           const std::string& key, bool default_value) {
  const auto it = options.find(key);
  if (it == options.end()) {
    return default_value;
  }
  const auto& value = it->second;
  return !(value == "0" || value == "false" || value == "False" ||
           value == "FALSE" || value == "no" || value == "No" || value == "NO");
}

std::optional<chat::TokenUsage> ExtractUsageFromNode(const Json& node) {
  if (!node.is_object()) {
    return std::nullopt;
  }

  chat::TokenUsage usage;
  if (node.contains("prompt_tokens") && node["prompt_tokens"].is_number()) {
    usage.prompt_tokens = node["prompt_tokens"].get<int>();
  }
  if (node.contains("completion_tokens") &&
      node["completion_tokens"].is_number()) {
    usage.completion_tokens = node["completion_tokens"].get<int>();
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

int AccumulateToolCallDelta(const Json& tool_call, StreamState& state) {
  if (!tool_call.contains("index")) {
    return -1;
  }

  const auto index = tool_call["index"].get<int>();
  auto& pending = state.pending_tool_calls[index];
  if (tool_call.contains("id") && tool_call["id"].is_string()) {
    pending.id = tool_call["id"].get<std::string>();
  }
  if (!tool_call.contains("function")) {
    return index;
  }

  const auto& function = tool_call["function"];
  if (function.contains("name") && function["name"].is_string()) {
    pending.name = function["name"].get<std::string>();
  }
  if (function.contains("arguments") && function["arguments"].is_string()) {
    pending.arguments_json += function["arguments"].get<std::string>();
  }
  return index;
}

void DispatchSseData(const std::string& data, StreamState& state) {
  try {
    const auto json = Json::parse(data);
    if (json.contains("error")) {
      (*state.sink)(
          chat::ChatEvent{chat::ErrorEvent{.text = json["error"].dump()}});
      return;
    }
    if (json.contains("usage") && json["usage"].is_object()) {
      if (auto usage = ExtractUsageFromNode(json["usage"])) {
        state.pending_usage = std::move(usage);
      }
    }
    if (!json.contains("choices") || json["choices"].empty()) {
      return;
    }

    const auto& choice = json["choices"][0];
    if (choice.contains("delta")) {
      const auto& delta = choice["delta"];
      if (delta.contains("content") && delta["content"].is_string()) {
        auto text = delta["content"].get<std::string>();
        if (!text.empty()) {
          (*state.sink)(
              chat::ChatEvent{chat::TextDeltaEvent{.text = std::move(text)}});
        }
      }

      if (delta.contains("tool_calls") && delta["tool_calls"].is_array()) {
        for (const auto& tool_call : delta["tool_calls"]) {
          const auto index = AccumulateToolCallDelta(tool_call, state);
          if (index < 0) {
            continue;
          }
          const auto& pending = state.pending_tool_calls[index];
          if (pending.id.empty()) {
            continue;
          }
          (*state.sink)(chat::ChatEvent{chat::ToolCallArgumentDeltaEvent{
              .tool_call_id = pending.id,
              .tool_name = pending.name,
              .arguments_json = pending.arguments_json,
          }});
        }
      }
    }

    // Any non-empty terminating finish_reason closes the choice and consumes
    // any tool_calls accumulated in deltas. Some compat servers (vLLM,
    // llama.cpp, ollama, certain GLM/ZAI deployments) emit tool_calls then
    // terminate with "stop"/"length" instead of "tool_calls"; flushing on
    // any terminator surfaces the model's intent rather than dropping it.
    if (choice.contains("finish_reason") &&
        choice["finish_reason"].is_string() &&
        !choice["finish_reason"].get<std::string>().empty()) {
      FlushPendingToolCalls(state, *state.sink);
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

int ExtractContextWindowFromModelNode(const Json& model) {
  // Priority order: OpenRouter's `context_length` first, then Anthropic's
  // `max_input_tokens`, then defensive aliases. First non-zero positive
  // integer wins.
  static constexpr std::array<std::string_view, 4> kContextWindowFields = {
      "context_length", "max_input_tokens", "max_context_length",
      "context_window"};
  for (const auto& field : kContextWindowFields) {
    const std::string field_str(field);
    if (model.contains(field_str) && model[field_str].is_number_integer()) {
      const auto value = model[field_str].get<long long>();
      if (value > 0) {
        return static_cast<int>(value);
      }
    }
  }
  // OpenRouter nests a per-provider variant under `top_provider`. Use it as a
  // fallback so the headline `context_length` still wins when both exist.
  if (model.contains("top_provider") && model["top_provider"].is_object()) {
    const auto& top = model["top_provider"];
    if (top.contains("context_length") &&
        top["context_length"].is_number_integer()) {
      const auto value = top["context_length"].get<long long>();
      if (value > 0) {
        return static_cast<int>(value);
      }
    }
  }
  return 0;
}

}  // namespace

std::string RoleToOpenAi(chat::ChatRole role) {
  switch (role) {
    case chat::ChatRole::System:
      return "system";
    case chat::ChatRole::User:
      return "user";
    case chat::ChatRole::Assistant:
      return "assistant";
    case chat::ChatRole::Tool:
      return "tool";
  }
  return "user";
}

Json BuildChatPayload(const chat::ChatRequest& request, bool stream,
                      const chat::ProviderConfig& config) {
  Json messages = Json::array();
  for (const auto& message : request.messages) {
    Json entry{{"role", RoleToOpenAi(message.role)}};
    if (message.role == chat::ChatRole::Tool) {
      entry["content"] = message.content;
      entry["tool_call_id"] = message.tool_call_id;
      if (!message.tool_name.empty()) {
        entry["name"] = message.tool_name;
      }
    } else {
      entry["content"] = message.content;
      if (!message.tool_calls.empty()) {
        Json tool_calls = Json::array();
        for (const auto& call : message.tool_calls) {
          tool_calls.push_back(
              {{"id", call.id},
               {"type", "function"},
               {"function",
                {{"name", call.name}, {"arguments", call.arguments_json}}}});
        }
        entry["tool_calls"] = std::move(tool_calls);
      }
    }
    messages.push_back(std::move(entry));
  }

  Json payload{{"model", request.model},
               {"messages", std::move(messages)},
               {"temperature", request.temperature},
               {"stream", stream}};

  if (stream &&
      ProviderOptionEnabled(config.options, "include_stream_usage", true)) {
    payload["stream_options"] = {{"include_usage", true}};
  }

  if (!request.tools.empty()) {
    Json tools = Json::array();
    for (const auto& tool : request.tools) {
      tools.push_back(
          {{"type", "function"},
           {"function",
            {{"name", tool.name},
             {"description", tool.description},
             {"parameters", ParseSchema(tool.parameters_schema_json)}}}});
    }
    payload["tools"] = std::move(tools);
    payload["tool_choice"] = "auto";
  }

  return payload;
}

std::vector<chat::ModelInfo> ParseModelsData(const std::string& data) {
  try {
    const auto json = Json::parse(data);
    const Json* models = nullptr;
    if (json.is_array()) {
      models = &json;
    } else if (json.contains("data") && json["data"].is_array()) {
      models = &json["data"];
    }
    if (models == nullptr) {
      return {};
    }

    std::vector<chat::ModelInfo> result;
    std::unordered_set<std::string> seen;
    for (const auto& model : *models) {
      std::string id;
      int context_window = 0;
      if (model.is_string()) {
        id = model.get<std::string>();
      } else if (model.is_object() && model.contains("id") &&
                 model["id"].is_string()) {
        id = model["id"].get<std::string>();
        context_window = ExtractContextWindowFromModelNode(model);
      }
      if (!id.empty() && seen.insert(id).second) {
        result.push_back(chat::ModelInfo{
            .id = id, .display_name = id, .context_window = context_window});
      }
    }
    return result;
  } catch (const std::exception&) {
    return {};
  }
}

chat::ChatEvent ParseStreamData(const std::string& data) {
  try {
    const auto json = Json::parse(data);
    if (json.contains("error")) {
      return chat::ChatEvent{chat::ErrorEvent{.text = json["error"].dump()}};
    }
    if (!json.contains("choices") || json["choices"].empty()) {
      return chat::ChatEvent{chat::TextDeltaEvent{}};
    }
    const auto& choice = json["choices"][0];
    if (!choice.contains("delta") || !choice["delta"].contains("content")) {
      return chat::ChatEvent{chat::TextDeltaEvent{}};
    }
    return chat::ChatEvent{chat::TextDeltaEvent{
        .text = choice["delta"]["content"].get<std::string>()}};
  } catch (const std::exception& error) {
    return chat::ChatEvent{chat::ErrorEvent{.text = error.what()}};
  }
}

std::optional<chat::TokenUsage> ParseUsageJson(const std::string& data) {
  try {
    const auto json = Json::parse(data);
    if (json.contains("usage")) {
      return ExtractUsageFromNode(json["usage"]);
    }
    return ExtractUsageFromNode(json);
  } catch (const std::exception&) {
    return std::nullopt;
  }
}

std::string ExtractBufferedText(const Json& response) {
  if (!response.contains("choices") || response["choices"].empty()) {
    return {};
  }
  const auto& choice = response["choices"][0];
  if (!choice.contains("message") || !choice["message"].contains("content")) {
    return {};
  }
  return choice["message"]["content"].get<std::string>();
}

std::optional<chat::TokenUsage> ExtractBufferedUsage(const Json& response) {
  if (!response.contains("usage")) {
    return std::nullopt;
  }
  return ExtractUsageFromNode(response["usage"]);
}

std::vector<chat::ToolCallRequest> ExtractBufferedToolCalls(
    const Json& response) {
  std::vector<chat::ToolCallRequest> calls;
  if (!response.contains("choices") || response["choices"].empty()) {
    return calls;
  }
  const auto& choice = response["choices"][0];
  if (!choice.contains("message") ||
      !choice["message"].contains("tool_calls")) {
    return calls;
  }
  const auto& tool_calls = choice["message"]["tool_calls"];
  if (!tool_calls.is_array()) {
    return calls;
  }

  for (const auto& tool_call : tool_calls) {
    if (!tool_call.contains("id") || !tool_call.contains("function")) {
      continue;
    }
    const auto& function = tool_call["function"];
    if (!function.contains("name") || !function.contains("arguments")) {
      continue;
    }
    calls.push_back(chat::ToolCallRequest{
        .id = tool_call["id"].get<std::string>(),
        .name = function["name"].get<std::string>(),
        .arguments_json = function["arguments"].get<std::string>()});
  }
  return calls;
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
    if (call.id.empty() || call.name.empty()) {
      continue;
    }
    calls.push_back(
        chat::ToolCallRequest{.id = call.id,
                              .name = call.name,
                              .arguments_json = call.arguments_json});
  }
  state.pending_tool_calls.clear();
  if (!calls.empty()) {
    sink(chat::ChatEvent{
        chat::ToolCallRequestedEvent{.tool_calls = std::move(calls)}});
  }
}

}  // namespace yac::provider::openai_compatible_protocol
