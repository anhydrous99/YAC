#include "provider/openai_chat_provider.hpp"

#include "chat/state_store.hpp"
#include "provider/http_sse_client.hpp"
#include "provider/openai_responses_protocol.hpp"

#include <array>
#include <cstdlib>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/utsname.h>
#endif

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

void CaptureHeaderLine(std::string_view line, HeaderState& state) {
  if (!line.starts_with("HTTP/")) {
    return;
  }

  const auto first_space = line.find(' ');
  if (first_space == std::string_view::npos) {
    return;
  }
  const auto second_space = line.find(' ', first_space + 1);
  const auto code_text =
      line.substr(first_space + 1, second_space == std::string_view::npos
                                       ? std::string_view::npos
                                       : second_space - first_space - 1);
  try {
    state.status_code = std::stol(std::string(code_text));
    state.saw_status_line = true;
  } catch (const std::exception& error) {
    (void)error;
  }
}

void ConsumeOAuthBody(std::string_view chunk, OAuthWriteState& state) {
  if (!state.header_state->saw_status_line) {
    state.pending_before_status.append(chunk.data(), chunk.size());
    return;
  }

  if (state.header_state->status_code >= 400) {
    state.error_body.append(chunk.data(), chunk.size());
    return;
  }

  if (!state.pending_before_status.empty()) {
    openai_responses_protocol::ConsumeSseChunk(state.pending_before_status,
                                               *state.stream_state);
    state.pending_before_status.clear();
  }
  openai_responses_protocol::ConsumeSseChunk(chunk, *state.stream_state);
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

[[nodiscard]] bool HasNonWhitespace(std::string_view value) {
  return value.find_first_not_of(" \t\n\r\f\v") != std::string_view::npos;
}

[[nodiscard]] chat::ChatRequest WithCodexInstructions(
    const chat::ChatRequest& request) {
  static constexpr std::string_view kCodexBaseInstructions =
      "You are YAC, a terminal coding assistant. Help the user with software "
      "engineering tasks while following the available tools, workspace "
      "instructions, and safety constraints.";

  chat::ChatRequest normalized = request;
  if (request.responses_instructions.has_value() &&
      HasNonWhitespace(*request.responses_instructions)) {
    normalized.responses_instructions = std::string(kCodexBaseInstructions) +
                                        "\n\n" +
                                        *request.responses_instructions;
    return normalized;
  }

  normalized.responses_instructions = std::string(kCodexBaseInstructions);
  return normalized;
}

[[nodiscard]] std::string PlatformDescription() {
#ifdef _WIN32
  SYSTEM_INFO info;
  GetNativeSystemInfo(&info);
  std::string arch = "unknown";
  if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_AMD64) {
    arch = "x86_64";
  } else if (info.wProcessorArchitecture == PROCESSOR_ARCHITECTURE_ARM64) {
    arch = "arm64";
  }
  return "Windows unknown; " + arch;
#else
  utsname info{};
  if (uname(&info) != 0) {
    return "unknown unknown; unknown";
  }
  return std::string(info.sysname) + " " + info.release + "; " + info.machine;
#endif
}

[[nodiscard]] std::string RuntimeUserAgent() {
  return std::string("opencode/") + YAC_VERSION + " (" + PlatformDescription() +
         ")";
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
    if (json.contains("detail") && json["detail"].is_string()) {
      return json["detail"].get<std::string>();
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
                                         bool retry_exhausted,
                                         const OpenAiOAuthAuth& auth) {
  std::ostringstream message;
  if (retry_exhausted) {
    message << "OpenAI OAuth request was unauthorized after refreshing once. "
            << "Sign in again.";
  } else {
    message << "OpenAI OAuth request failed with HTTP " << status_code << ".";
  }
  const auto detail = ParseErrorMessage(response_body);
  if (!detail.empty()) {
    message << " " << detail;
  }
  std::string text = message.str();
  for (const auto& secret : {auth.access_token, auth.refresh_token}) {
    std::string::size_type pos = 0;
    while (!secret.empty() &&
           (pos = text.find(secret, pos)) != std::string::npos) {
      text.replace(pos, secret.size(), "[REDACTED]");
      pos += std::string_view("[REDACTED]").size();
    }
  }
  return text;
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
  const auto normalized_request = WithCodexInstructions(request);
  auto payload = openai_responses_protocol::BuildResponsesPayload(
                     normalized_request, config)
                     .dump();
  openai_responses_protocol::StreamState stream_state{.sink = &sink};
  HeaderState header_state;
  OAuthWriteState write_state{.header_state = &header_state,
                              .stream_state = &stream_state};

  std::vector<std::string> headers = {"Content-Type: application/json",
                                      "Accept: text/event-stream"};
  const auto auth_header = "Authorization: Bearer " + auth.access_token;
  headers.push_back(auth_header);
  headers.emplace_back("originator: opencode");
  const auto user_agent = "User-Agent: " + RuntimeUserAgent();
  headers.push_back(user_agent);
  if (request.session_id.has_value() && !request.session_id->empty()) {
    const auto session_header = "session_id: " + *request.session_id;
    headers.push_back(session_header);
  }
  if (auth.account_id.has_value() && !auth.account_id->empty()) {
    const auto account_header = "ChatGPT-Account-Id: " + *auth.account_id;
    headers.push_back(account_header);
  }

  const auto url = OAuthRequestUrl(oauth_base_url);
  const auto response = internal::PerformHttpStreamPost(
      internal::HttpStreamPostRequest{
          .url = url,
          .payload = std::move(payload),
          .headers = std::move(headers),
          .on_header_line =
              [&header_state](std::string_view line) {
                CaptureHeaderLine(line, header_state);
              },
          .on_body_chunk =
              [&write_state](std::string_view chunk) {
                ConsumeOAuthBody(chunk, write_state);
              }},
      stop_token);
  if (response.cancelled) {
    return {};
  }

  if (response.status_code >= 400) {
    return {.status_code = response.status_code,
            .error_body = std::move(write_state.error_body)};
  }

  if (!write_state.pending_before_status.empty()) {
    openai_responses_protocol::ConsumeSseChunk(
        write_state.pending_before_status, stream_state);
  }
  openai_responses_protocol::FlushPendingToolCalls(stream_state, sink);
  if (stream_state.pending_usage.has_value()) {
    sink(chat::ChatEvent{chat::UsageReportedEvent{
        .provider_id = config.id,
        .model = request.model,
        .usage = std::move(*stream_state.pending_usage)}});
  }
  return {.status_code = response.status_code};
}

}  // namespace

bool OpenAiChatProvider::IsOAuthModelAllowed(std::string_view model_id) {
  static constexpr std::array<std::string_view, 6> kStaticModels = {
      "gpt-5.5", "gpt-5.2",     "gpt-5.3-codex", "gpt-5.3-codex-spark",
      "gpt-5.4", "gpt-5.4-mini"};
  for (const std::string_view static_model : kStaticModels) {
    if (model_id == static_model) {
      return true;
    }
  }

  static const std::regex dynamic_model_regex(R"(^gpt-(\d+\.\d+))");
  std::cmatch match;
  const char* begin = model_id.data();
  const char* end = begin + model_id.size();
  if (!std::regex_search(begin, end, match, dynamic_model_regex)) {
    return false;
  }
  try {
    return std::stod(match[1].str()) > 5.4;
  } catch (const std::exception& error) {
    (void)error;
    return false;
  }
}

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
  if (!config_.api_key.empty()) {
    return config_.api_key;
  }
  if (const auto stored = auth_flow_->LoadStoredAuth(); stored.has_value()) {
    if (const auto* api_auth = std::get_if<OpenAiApiKeyAuth>(&stored->auth)) {
      return api_auth->key;
    }
  }
  if (const auto state_auth = LoadStateStoreOpenAiAuth();
      state_auth.has_value()) {
    if (const auto* api_auth = std::get_if<OpenAiApiKeyAuth>(&*state_auth)) {
      return api_auth->key;
    }
  }
  return OpenAiCompatibleChatProvider::ResolveApiKey();
}

OpenAiChatProvider::EffectiveAuth OpenAiChatProvider::ResolveEffectiveAuth()
    const {
  if (const char* env = std::getenv(config_.api_key_env.c_str())) {
    if (*env != '\0') {
      return EffectiveAuth{.api_key = env};
    }
  }
  if (!config_.api_key.empty()) {
    return EffectiveAuth{.api_key = config_.api_key};
  }
  if (const auto stored = auth_flow_->LoadStoredAuth(); stored.has_value()) {
    if (const auto* api_auth = std::get_if<OpenAiApiKeyAuth>(&stored->auth)) {
      return EffectiveAuth{.api_key = api_auth->key};
    }
    return EffectiveAuth{
        .oauth = std::get<OpenAiOAuthAuth>(stored->auth),
    };
  }
  if (const auto state_auth = LoadStateStoreOpenAiAuth();
      state_auth.has_value()) {
    if (const auto* api_auth = std::get_if<OpenAiApiKeyAuth>(&*state_auth)) {
      return EffectiveAuth{.api_key = api_auth->key};
    }
    return EffectiveAuth{.oauth = std::get<OpenAiOAuthAuth>(*state_auth)};
  }
  if (const auto state_api_key = OpenAiCompatibleChatProvider::ResolveApiKey();
      !state_api_key.empty()) {
    return EffectiveAuth{.api_key = state_api_key};
  }
  return {};
}

std::optional<OpenAiAuth> OpenAiChatProvider::LoadStateStoreOpenAiAuth() const {
  if (config_.state_store == nullptr) {
    return std::nullopt;
  }
  if (config_.profile_id.has_value()) {
    if (const auto credential = config_.state_store->LoadProviderCredential(
            config_.id, std::string_view(*config_.profile_id),
            chat::StateCredentialType::OpenAiAuth)) {
      try {
        return ParseOpenAiAuth(credential->secret_json);
      } catch (const std::exception& error) {
        (void)error;
        throw std::runtime_error("Stored OpenAI auth credential is invalid.");
      }
    }
  }
  if (const auto credential = config_.state_store->LoadProviderCredential(
          config_.id, std::nullopt, chat::StateCredentialType::OpenAiAuth)) {
    try {
      return ParseOpenAiAuth(credential->secret_json);
    } catch (const std::exception& error) {
      (void)error;
      throw std::runtime_error("Stored OpenAI auth credential is invalid.");
    }
  }
  return std::nullopt;
}

OpenAiChatProvider::EffectiveAuthSource
OpenAiChatProvider::ResolveEffectiveAuthSource() const {
  if (const char* env = std::getenv(config_.api_key_env.c_str())) {
    if (*env != '\0') {
      return EffectiveAuthSource::EnvApiKey;
    }
  }
  if (!config_.api_key.empty()) {
    return EffectiveAuthSource::InlineApiKey;
  }
  if (const auto stored = auth_flow_->LoadStoredAuth(); stored.has_value()) {
    if (std::holds_alternative<OpenAiApiKeyAuth>(stored->auth)) {
      return EffectiveAuthSource::StoredApiKey;
    }
    return EffectiveAuthSource::StoredOAuth;
  }
  if (const auto state_auth = LoadStateStoreOpenAiAuth();
      state_auth.has_value()) {
    if (std::holds_alternative<OpenAiApiKeyAuth>(*state_auth)) {
      return EffectiveAuthSource::StateStoreApiKey;
    }
    return EffectiveAuthSource::StateStoreOAuth;
  }
  if (const auto state_api_key = OpenAiCompatibleChatProvider::ResolveApiKey();
      !state_api_key.empty()) {
    return EffectiveAuthSource::StateStoreApiKey;
  }
  return EffectiveAuthSource::None;
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
    throw std::runtime_error(BuildHttpError(
        result.status_code, result.error_body,
        result.status_code == 401 && retried_after_401, current_auth));
  }
}

}  // namespace yac::provider
