#include "provider/openai_chat_provider.hpp"

#include <cstdlib>
#include <curl/curl.h>
#include <map>
#include <memory>
#include <openai.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <unordered_set>
#include <utility>

namespace yac::provider {
namespace {

using Json = openai::_detail::Json;

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

Json BuildChatPayload(const chat::ChatRequest& request, bool stream,
                      const chat::ProviderConfig& config) {
  Json messages = Json::array();
  for (const auto& message : request.messages) {
    Json entry{{"role", OpenAiChatProvider::RoleToOpenAi(message.role)}};
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

size_t WriteString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const auto bytes = size * nmemb;
  auto* buffer = static_cast<std::string*>(userdata);
  buffer->append(ptr, bytes);
  return bytes;
}

struct StreamState {
  std::string buffer;
  ChatEventSink* sink = nullptr;
  struct PendingToolCall {
    std::string id;
    std::string name;
    std::string arguments_json;
  };
  std::map<int, PendingToolCall> pending_tool_calls;
  std::optional<chat::TokenUsage> pending_usage;
};

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
  return usage;
}

std::vector<chat::ToolCallRequest> PendingToolCalls(const StreamState& state) {
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
  return calls;
}

void AccumulateToolCallDelta(const Json& tool_call, StreamState& state) {
  if (!tool_call.contains("index")) {
    return;
  }
  const auto index = tool_call["index"].get<int>();
  auto& pending = state.pending_tool_calls[index];
  if (tool_call.contains("id") && tool_call["id"].is_string()) {
    pending.id = tool_call["id"].get<std::string>();
  }
  if (!tool_call.contains("function")) {
    return;
  }
  const auto& function = tool_call["function"];
  if (function.contains("name") && function["name"].is_string()) {
    pending.name = function["name"].get<std::string>();
  }
  if (function.contains("arguments") && function["arguments"].is_string()) {
    pending.arguments_json += function["arguments"].get<std::string>();
  }
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
          AccumulateToolCallDelta(tool_call, state);
        }
      }
    }

    if (choice.contains("finish_reason") &&
        choice["finish_reason"].is_string() &&
        choice["finish_reason"].get<std::string>() == "tool_calls") {
      auto calls = PendingToolCalls(state);
      if (!calls.empty()) {
        (*state.sink)(chat::ChatEvent{
            chat::ToolCallRequestedEvent{.tool_calls = std::move(calls)}});
      }
      state.pending_tool_calls.clear();
    }
  } catch (const std::exception& error) {
    (*state.sink)(chat::ChatEvent{chat::ErrorEvent{.text = error.what()}});
  }
}

void DispatchSseLine(const std::string& line, StreamState& state) {
  constexpr std::string_view kPrefix = "data: ";
  if (!line.starts_with(kPrefix)) {
    return;
  }

  auto data = line.substr(kPrefix.size());
  if (data == "[DONE]") {
    return;
  }

  DispatchSseData(data, state);
}

size_t WriteStream(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const auto bytes = size * nmemb;
  auto* state = static_cast<StreamState*>(userdata);
  state->buffer.append(ptr, bytes);

  size_t pos = 0;
  while ((pos = state->buffer.find('\n')) != std::string::npos) {
    auto line = state->buffer.substr(0, pos);
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    state->buffer.erase(0, pos + 1);
    DispatchSseLine(line, *state);
  }

  return bytes;
}

struct ProgressState {
  std::stop_token* stop_token = nullptr;
};

int ProgressCallback(void* clientp, curl_off_t download_total,
                     curl_off_t download_now, curl_off_t upload_total,
                     curl_off_t upload_now) {
  (void)download_total;
  (void)download_now;
  (void)upload_total;
  (void)upload_now;

  const auto* state = static_cast<ProgressState*>(clientp);
  return state->stop_token->stop_requested() ? 1 : 0;
}

std::string TrimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
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

}  // namespace

OpenAiChatProvider::OpenAiChatProvider(chat::ProviderConfig config)
    : config_(std::move(config)) {}

std::string OpenAiChatProvider::Id() const {
  return config_.id;
}

std::vector<chat::ModelInfo> OpenAiChatProvider::ListModels(
    std::chrono::milliseconds timeout) {
  const auto api_key = ResolveApiKey();
  if (api_key.empty()) {
    throw std::runtime_error(config_.api_key_env + " is not set.");
  }

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed.");
  }

  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  std::string response;
  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");
  const auto auth_header = "Authorization: Bearer " + api_key;
  headers = curl_slist_append(headers, auth_header.c_str());

  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  const auto url = ModelsUrl();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteString);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS,
                   static_cast<long>(timeout.count()));

  const auto result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(result));
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (status >= 400) {
    std::ostringstream message;
    message << config_.id << " model discovery failed with HTTP " << status
            << ".";
    throw std::runtime_error(message.str());
  }

  return ParseModelsData(response);
}

void OpenAiChatProvider::CompleteStream(const chat::ChatRequest& request,
                                        ChatEventSink sink,
                                        std::stop_token stop_token) {
  try {
    if (request.stream) {
      CompleteStreaming(request, sink, stop_token);
      return;
    }
    CompleteBuffered(request, sink);
  } catch (const std::exception& error) {
    sink(chat::ChatEvent{chat::ErrorEvent{.text = error.what(),
                                          .provider_id = config_.id,
                                          .model = request.model}});
  }
}

std::string OpenAiChatProvider::RoleToOpenAi(chat::ChatRole role) {
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

std::vector<chat::ModelInfo> OpenAiChatProvider::ParseModelsData(
    const std::string& data) {
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
      if (model.is_string()) {
        id = model.get<std::string>();
      } else if (model.contains("id") && model["id"].is_string()) {
        id = model["id"].get<std::string>();
      }

      if (!id.empty() && seen.insert(id).second) {
        result.push_back(chat::ModelInfo{.id = id, .display_name = id});
      }
    }

    return result;
  } catch (const std::exception&) {
    return {};
  }
}

chat::ChatEvent OpenAiChatProvider::ParseStreamData(const std::string& data) {
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

std::optional<chat::TokenUsage> OpenAiChatProvider::ParseUsageJson(
    const std::string& data) {
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

void OpenAiChatProvider::CompleteBuffered(const chat::ChatRequest& request,
                                          ChatEventSink sink) {
  openai::OpenAI client(ResolveApiKey(), "", true, config_.base_url);
  const auto response =
      client.chat.create(BuildChatPayload(request, false, config_));
  const auto text = ExtractBufferedText(response);
  if (!text.empty()) {
    sink(chat::ChatEvent{chat::TextDeltaEvent{
        .text = text, .provider_id = config_.id, .model = request.model}});
  }
  auto tool_calls = ExtractBufferedToolCalls(response);
  if (!tool_calls.empty()) {
    sink(chat::ChatEvent{
        chat::ToolCallRequestedEvent{.tool_calls = std::move(tool_calls)}});
  }
  if (auto usage = ExtractBufferedUsage(response)) {
    sink(chat::ChatEvent{chat::UsageReportedEvent{.provider_id = config_.id,
                                                  .model = request.model,
                                                  .usage = std::move(*usage)}});
  }
}

void OpenAiChatProvider::CompleteStreaming(const chat::ChatRequest& request,
                                           ChatEventSink sink,
                                           std::stop_token stop_token) {
  const auto api_key = ResolveApiKey();
  if (api_key.empty()) {
    throw std::runtime_error(config_.api_key_env + " is not set.");
  }

  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed.");
  }

  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  auto payload = BuildChatPayload(request, true, config_).dump();
  StreamState stream_state{.sink = &sink};
  ProgressState progress_state{.stop_token = &stop_token};

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: text/event-stream");
  const auto auth_header = "Authorization: Bearer " + api_key;
  headers = curl_slist_append(headers, auth_header.c_str());

  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  const auto url = CompletionUrl();
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteStream);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &stream_state);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_state);

  const auto result = curl_easy_perform(curl);
  if (stop_token.stop_requested()) {
    return;
  }
  if (result != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(result));
  }

  long status = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
  if (status >= 400) {
    std::ostringstream message;
    message << "OpenAI request failed with HTTP " << status << ".";
    throw std::runtime_error(message.str());
  }

  if (stream_state.pending_usage.has_value()) {
    sink(chat::ChatEvent{chat::UsageReportedEvent{
        .provider_id = config_.id,
        .model = request.model,
        .usage = std::move(*stream_state.pending_usage)}});
  }
}

std::string OpenAiChatProvider::ResolveApiKey() const {
  if (!config_.api_key.empty()) {
    return config_.api_key;
  }
  if (const char* env = std::getenv(config_.api_key_env.c_str())) {
    return env;
  }
  return {};
}

std::string OpenAiChatProvider::CompletionUrl() const {
  return TrimTrailingSlash(config_.base_url) + "/chat/completions";
}

std::string OpenAiChatProvider::ModelsUrl() const {
  return TrimTrailingSlash(config_.base_url) + "/models";
}

}  // namespace yac::provider
