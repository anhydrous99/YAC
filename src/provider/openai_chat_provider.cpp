#include "provider/openai_chat_provider.hpp"
#include "provider/openai_chat_protocol.hpp"

#include <cstdlib>
#include <curl/curl.h>
#include <memory>
#include <openai.hpp>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace yac::provider {
namespace {

using Json = openai_protocol::Json;

size_t WriteString(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const auto bytes = size * nmemb;
  auto* buffer = static_cast<std::string*>(userdata);
  buffer->append(ptr, bytes);
  return bytes;
}

size_t WriteStream(char* ptr, size_t size, size_t nmemb, void* userdata) {
  const auto bytes = size * nmemb;
  auto* state = static_cast<openai_protocol::StreamState*>(userdata);
  openai_protocol::ConsumeSseChunk(std::string_view(ptr, bytes), *state);
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

  return openai_protocol::ParseModelsData(response);
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
  return openai_protocol::RoleToOpenAi(role);
}

std::vector<chat::ModelInfo> OpenAiChatProvider::ParseModelsData(
    const std::string& data) {
  return openai_protocol::ParseModelsData(data);
}

chat::ChatEvent OpenAiChatProvider::ParseStreamData(const std::string& data) {
  return openai_protocol::ParseStreamData(data);
}

std::optional<chat::TokenUsage> OpenAiChatProvider::ParseUsageJson(
    const std::string& data) {
  return openai_protocol::ParseUsageJson(data);
}

void OpenAiChatProvider::CompleteBuffered(const chat::ChatRequest& request,
                                          ChatEventSink sink) {
  openai::OpenAI client(ResolveApiKey(), "", true, config_.base_url);
  const auto response =
      client.chat.create(openai_protocol::BuildChatPayload(request, false, config_));
  const auto text = openai_protocol::ExtractBufferedText(response);
  if (!text.empty()) {
    sink(chat::ChatEvent{chat::TextDeltaEvent{
        .text = text, .provider_id = config_.id, .model = request.model}});
  }
  auto tool_calls = openai_protocol::ExtractBufferedToolCalls(response);
  if (!tool_calls.empty()) {
    sink(chat::ChatEvent{
        chat::ToolCallRequestedEvent{.tool_calls = std::move(tool_calls)}});
  }
  if (auto usage = openai_protocol::ExtractBufferedUsage(response)) {
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

  auto payload = openai_protocol::BuildChatPayload(request, true, config_).dump();
  openai_protocol::StreamState stream_state{.sink = &sink};
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
