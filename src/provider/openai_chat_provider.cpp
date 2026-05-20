#include "provider/openai_chat_provider.hpp"

#include "provider/openai_responses_protocol.hpp"

#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <memory>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace yac::provider {
namespace {

#ifndef YAC_VERSION
#define YAC_VERSION "dev"
#endif

using Json = nlohmann::json;

struct HeaderState {
  long status_code = 0;
  bool saw_status_line = false;
};

struct OAuthWriteState {
  HeaderState* header_state = nullptr;
  openai_responses_protocol::StreamState* stream_state = nullptr;
  std::string pending_before_status;
  std::string error_body;
};

[[nodiscard]] size_t WriteString(char* ptr, size_t size, size_t nmemb,
                                 void* userdata) {
  const auto bytes = size * nmemb;
  auto* buffer = static_cast<std::string*>(userdata);
  buffer->append(ptr, bytes);
  return bytes;
}

[[nodiscard]] size_t CaptureHeaders(char* buffer, size_t size, size_t nitems,
                                    void* userdata) {
  const auto bytes = size * nitems;
  auto* state = static_cast<HeaderState*>(userdata);
  std::string_view line(buffer, bytes);
  if (!line.starts_with("HTTP/")) {
    return bytes;
  }

  const auto first_space = line.find(' ');
  if (first_space == std::string_view::npos) {
    return bytes;
  }
  const auto second_space = line.find(' ', first_space + 1);
  const auto code_text = line.substr(
      first_space + 1,
      second_space == std::string_view::npos ? std::string_view::npos
                                             : second_space - first_space - 1);
  try {
    state->status_code = std::stol(std::string(code_text));
    state->saw_status_line = true;
  } catch (const std::exception& error) {
    (void)error;
  }
  return bytes;
}

[[nodiscard]] size_t WriteOAuthBody(char* ptr, size_t size, size_t nmemb,
                                    void* userdata) {
  const auto bytes = size * nmemb;
  auto* state = static_cast<OAuthWriteState*>(userdata);
  const std::string_view chunk(ptr, bytes);
  if (!state->header_state->saw_status_line) {
    state->pending_before_status.append(chunk.data(), chunk.size());
    return bytes;
  }

  if (state->header_state->status_code >= 400) {
    state->error_body.append(chunk.data(), chunk.size());
    return bytes;
  }

  if (!state->pending_before_status.empty()) {
    openai_responses_protocol::ConsumeSseChunk(state->pending_before_status,
                                               *state->stream_state);
    state->pending_before_status.clear();
  }
  openai_responses_protocol::ConsumeSseChunk(chunk, *state->stream_state);
  return bytes;
}

[[nodiscard]] std::string TrimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

[[nodiscard]] std::string OAuthRequestUrl(std::string_view oauth_base_url) {
  return TrimTrailingSlash(std::string(oauth_base_url)) +
         openai_responses_protocol::ResponsesPath();
}

[[nodiscard]] std::string ParseErrorMessage(std::string_view body) {
  if (body.empty()) {
    return {};
  }
  try {
    const Json json = Json::parse(body);
    if (json.contains("message") && json["message"].is_string()) {
      return json["message"].get<std::string>();
    }
    if (json.contains("error")) {
      const auto& error = json["error"];
      if (error.is_string()) {
        return error.get<std::string>();
      }
      if (error.is_object() && error.contains("message") &&
          error["message"].is_string()) {
        return error["message"].get<std::string>();
      }
    }
  } catch (const std::exception& error) {
    (void)error;
  }
  return std::string(body);
}

[[nodiscard]] std::string BuildHttpError(long status_code,
                                         std::string_view response_body,
                                         bool retry_exhausted) {
  std::ostringstream message;
  if (retry_exhausted) {
    message
        << "OpenAI OAuth request was unauthorized after refreshing once. "
        << "Sign in again.";
  } else {
    message << "OpenAI OAuth request failed with HTTP " << status_code << ".";
  }
  const auto detail = ParseErrorMessage(response_body);
  if (!detail.empty()) {
    message << " " << detail;
  }
  return message.str();
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

struct OAuthAttemptResult {
  long status_code = 0;
  std::string error_body;
};

OAuthAttemptResult ExecuteOAuthAttempt(const chat::ChatRequest& request,
                                       const chat::ProviderConfig& config,
                                       const std::string& oauth_base_url,
                                       const OpenAiOAuthAuth& auth,
                                       ChatEventSink sink,
                                       std::stop_token stop_token) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed.");
  }
  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  auto payload = openai_responses_protocol::BuildResponsesPayload(request, config)
                     .dump();
  openai_responses_protocol::StreamState stream_state{.sink = &sink};
  HeaderState header_state;
  OAuthWriteState write_state{.header_state = &header_state,
                              .stream_state = &stream_state};
  ProgressState progress_state{.stop_token = &stop_token};

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: text/event-stream");
  const auto auth_header = "Authorization: Bearer " + auth.access_token;
  headers = curl_slist_append(headers, auth_header.c_str());
  headers = curl_slist_append(headers, "originator: yac");
  const auto user_agent = std::string("User-Agent: yac/") + YAC_VERSION;
  headers = curl_slist_append(headers, user_agent.c_str());
  if (auth.account_id.has_value() && !auth.account_id->empty()) {
    const auto account_header =
        "ChatGPT-Account-Id: " + *auth.account_id;
    headers = curl_slist_append(headers, account_header.c_str());
  }
  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  const auto url = OAuthRequestUrl(oauth_base_url);
  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, CaptureHeaders);
  curl_easy_setopt(curl, CURLOPT_HEADERDATA, &header_state);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteOAuthBody);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &write_state);
  curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
  curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, ProgressCallback);
  curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &progress_state);

  const auto result = curl_easy_perform(curl);
  if (stop_token.stop_requested()) {
    return {};
  }
  if (result != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(result));
  }

  long status_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);
  if (status_code >= 400) {
    return {.status_code = status_code,
            .error_body = std::move(write_state.error_body)};
  }

  if (!write_state.pending_before_status.empty()) {
    openai_responses_protocol::ConsumeSseChunk(write_state.pending_before_status,
                                               stream_state);
  }
  openai_responses_protocol::FlushPendingToolCalls(stream_state, sink);
  if (stream_state.pending_usage.has_value()) {
    sink(chat::ChatEvent{chat::UsageReportedEvent{
        .provider_id = config.id,
        .model = request.model,
        .usage = std::move(*stream_state.pending_usage)}});
  }
  return {.status_code = status_code};
}

}  // namespace

OpenAiChatProvider::OpenAiChatProvider(chat::ProviderConfig config)
    : OpenAiChatProvider(std::move(config), Dependencies{}) {}

OpenAiChatProvider::OpenAiChatProvider(chat::ProviderConfig config,
                                       Dependencies dependencies)
    : OpenAiCompatibleChatProvider(config),
      config_(std::move(config)),
      auth_flow_(std::move(dependencies.auth_flow)),
      oauth_base_url_(std::move(dependencies.oauth_base_url)) {
  if (auth_flow_ == nullptr) {
    auth_flow_ = std::make_shared<OpenAiAuthFlow>();
  }
  if (oauth_base_url_.empty()) {
    oauth_base_url_ = "https://chatgpt.com";
  }
}

std::vector<chat::ModelInfo> OpenAiChatProvider::ListModels(
    std::chrono::milliseconds timeout) {
  (void)timeout;
  const auto auth = ResolveEffectiveAuth();
  if (auth.oauth.has_value()) {
    return OAuthModelAllowlist();
  }
  return OpenAiCompatibleChatProvider::ListModels(timeout);
}

void OpenAiChatProvider::CompleteStream(const chat::ChatRequest& request,
                                        ChatEventSink sink,
                                        std::stop_token stop_token) {
  try {
    const auto auth = ResolveEffectiveAuth();
    if (auth.oauth.has_value()) {
      CompleteWithOAuth(request, sink, stop_token, *auth.oauth);
      return;
    }
    OpenAiCompatibleChatProvider::CompleteStream(request, std::move(sink),
                                                 stop_token);
  } catch (const std::exception& error) {
    sink(chat::ChatEvent{chat::ErrorEvent{.text = error.what(),
                                          .provider_id = config_.id,
                                          .model = request.model}});
  }
}

std::string OpenAiChatProvider::ResolveApiKey() const {
  if (const char* env = std::getenv(config_.api_key_env.c_str())) {
    if (*env != '\0') {
      return env;
    }
  }
  if (const auto stored = auth_flow_->LoadStoredAuth(); stored.has_value()) {
    if (const auto* api_auth = std::get_if<OpenAiApiKeyAuth>(&stored->auth)) {
      return api_auth->key;
    }
  }
  return config_.api_key;
}

OpenAiChatProvider::EffectiveAuth OpenAiChatProvider::ResolveEffectiveAuth()
    const {
  if (const char* env = std::getenv(config_.api_key_env.c_str())) {
    if (*env != '\0') {
      return EffectiveAuth{.api_key = env};
    }
  }
  if (const auto stored = auth_flow_->LoadStoredAuth(); stored.has_value()) {
    if (const auto* api_auth = std::get_if<OpenAiApiKeyAuth>(&stored->auth)) {
      return EffectiveAuth{.api_key = api_auth->key};
    }
    return EffectiveAuth{
        .oauth = std::get<OpenAiOAuthAuth>(stored->auth),
    };
  }
  if (!config_.api_key.empty()) {
    return EffectiveAuth{.api_key = config_.api_key};
  }
  return {};
}

std::vector<chat::ModelInfo> OpenAiChatProvider::OAuthModelAllowlist() {
  return {
      {.id = "gpt-5.5", .display_name = "gpt-5.5"},
      {.id = "gpt-5.2", .display_name = "gpt-5.2"},
      {.id = "gpt-5.3-codex", .display_name = "gpt-5.3-codex"},
      {.id = "gpt-5.3-codex-spark", .display_name = "gpt-5.3-codex-spark"},
      {.id = "gpt-5.4", .display_name = "gpt-5.4"},
      {.id = "gpt-5.4-mini", .display_name = "gpt-5.4-mini"},
  };
}

void OpenAiChatProvider::CompleteWithOAuth(const chat::ChatRequest& request,
                                           ChatEventSink sink,
                                           std::stop_token stop_token,
                                           const OpenAiOAuthAuth& auth) const {
  OpenAiOAuthAuth current_auth = auth_flow_->RefreshIfNeeded(auth);
  bool retried_after_401 = false;

  while (!stop_token.stop_requested()) {
    const auto result = ExecuteOAuthAttempt(request, config_, oauth_base_url_,
                                            current_auth, sink, stop_token);
    if (stop_token.stop_requested()) {
      return;
    }
    if (result.status_code < 400) {
      return;
    }
    if (result.status_code == 401 && !retried_after_401) {
      retried_after_401 = true;
      current_auth = auth_flow_->Refresh(current_auth);
      continue;
    }
    throw std::runtime_error(BuildHttpError(result.status_code,
                                            result.error_body,
                                            result.status_code == 401 &&
                                                retried_after_401));
  }
}

}  // namespace yac::provider
