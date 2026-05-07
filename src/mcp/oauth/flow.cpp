#include "mcp/oauth/flow.hpp"

#include "mcp/json_helpers.hpp"

#include <array>
#include <chrono>
#include <curl/curl.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace yac::mcp::oauth {
namespace {

using Json = nlohmann::json;

struct HttpResponse {
  long status_code = 0;
  std::string body;
};

[[nodiscard]] bool IsAllowedEndpointUrl(std::string_view url) {
  if (url.starts_with("https://")) {
    return true;
  }

  return url.starts_with("http://127.0.0.1") ||
         url.starts_with("http://localhost");
}

void ValidateEndpointUrl(std::string_view url, std::string_view field_name) {
  if (!IsAllowedEndpointUrl(url)) {
    throw std::runtime_error(std::string(field_name) +
                             " must use https:// or loopback http://");
  }
}

std::size_t WriteCallback(char* ptr, std::size_t size, std::size_t nmemb,
                          void* userdata) {
  const std::size_t bytes = size * nmemb;
  auto* body = static_cast<std::string*>(userdata);
  body->append(ptr, bytes);
  return bytes;
}

[[nodiscard]] std::string UrlEncode(std::string_view value) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }

  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  char* escaped =
      curl_easy_escape(curl, value.data(), static_cast<int>(value.size()));
  if (escaped == nullptr) {
    throw std::runtime_error("curl_easy_escape failed");
  }

  const auto cleanup_escape = [](char* value_ptr) { curl_free(value_ptr); };
  std::unique_ptr<char, decltype(cleanup_escape)> escaped_handle(
      escaped, cleanup_escape);
  return escaped;
}

[[nodiscard]] std::string JoinScopes(const std::vector<std::string>& scopes) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < scopes.size(); ++index) {
    if (index > 0) {
      stream << ' ';
    }
    stream << scopes[index];
  }
  return stream.str();
}

[[nodiscard]] std::string BuildFormBody(
    const std::vector<std::pair<std::string, std::string>>& form_fields) {
  std::ostringstream stream;
  for (std::size_t index = 0; index < form_fields.size(); ++index) {
    if (index > 0) {
      stream << '&';
    }
    stream << UrlEncode(form_fields[index].first) << '='
           << UrlEncode(form_fields[index].second);
  }
  return stream.str();
}

[[nodiscard]] HttpResponse PostForm(
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>>& form_fields) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }

  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  std::string response_body;
  const std::string payload = BuildFormBody(form_fields);

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(
      headers, "Content-Type: application/x-www-form-urlencoded");
  headers = curl_slist_append(headers, "Accept: application/json");
  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  const std::string url_string(url);
  ValidateEndpointUrl(url_string, "OAuth token_url");
  curl_easy_setopt(curl, CURLOPT_URL, url_string.c_str());
  curl_easy_setopt(curl, CURLOPT_POST, 1L);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
  curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);

  const CURLcode result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(result));
  }

  HttpResponse response;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
  response.body = std::move(response_body);
  return response;
}

[[nodiscard]] std::string ResolveResource(const OAuthConfig& config,
                                          std::string_view resource) {
  if (!resource.empty()) {
    return std::string(resource);
  }
  return config.resource_url;
}

[[nodiscard]] OAuthTokens ParseTokens(std::string_view response_body) {
  Json payload =
      ::yac::mcp::ParseJsonOrThrow(response_body, "OAuth token response");

  if (!payload.contains("access_token") ||
      !payload["access_token"].is_string()) {
    throw std::runtime_error("OAuth token response missing access_token");
  }

  const auto now = std::chrono::system_clock::now();
  const auto expires_in =
      payload.contains("expires_in") &&
              payload["expires_in"].is_number_integer()
          ? std::chrono::seconds(payload["expires_in"].get<int>())
          : std::chrono::seconds{0};

  OAuthTokens tokens;
  tokens.access_token = payload["access_token"].get<std::string>();
  if (payload.contains("refresh_token") &&
      payload["refresh_token"].is_string()) {
    tokens.refresh_token = payload["refresh_token"].get<std::string>();
  }
  if (payload.contains("token_type") && payload["token_type"].is_string()) {
    tokens.token_type = payload["token_type"].get<std::string>();
  }
  if (payload.contains("scope") && payload["scope"].is_string()) {
    tokens.scope = payload["scope"].get<std::string>();
  }
  tokens.expires_at = now + expires_in;
  return tokens;
}

}  // namespace

std::string OAuthFlow::BuildAuthorizationUrl(const OAuthConfig& config,
                                             std::string_view code_challenge,
                                             std::string_view state,
                                             std::string_view redirect_uri,
                                             std::string_view resource) {
  ValidateEndpointUrl(config.authorization_url, "OAuth authorization_url");

  std::vector<std::pair<std::string, std::string>> query = {
      {"response_type", "code"},
      {"client_id", config.client_id},
      {"redirect_uri", std::string(redirect_uri)},
      {"code_challenge", std::string(code_challenge)},
      {"code_challenge_method", "S256"},
      {"state", std::string(state)}};

  const std::string scope = JoinScopes(config.scopes);
  if (!scope.empty()) {
    query.emplace_back("scope", scope);
  }

  const std::string resolved_resource = ResolveResource(config, resource);
  if (!resolved_resource.empty()) {
    query.emplace_back("resource", resolved_resource);
  }

  std::ostringstream url;
  url << config.authorization_url;
  url << (config.authorization_url.find('?') == std::string::npos ? '?' : '&');
  for (std::size_t index = 0; index < query.size(); ++index) {
    if (index > 0) {
      url << '&';
    }
    url << UrlEncode(query[index].first) << '='
        << UrlEncode(query[index].second);
  }

  {
    std::scoped_lock lock(mutex_);
    state_ = State::AwaitingCallback;
    expected_state_ = state;
    state_validated_ = false;
  }
  return url.str();
}

void OAuthFlow::ValidateState(std::string_view callback_state) {
  std::scoped_lock lock(mutex_);
  if (state_ != State::AwaitingCallback) {
    throw std::runtime_error(
        "OAuth state validation requested before authorization callback");
  }
  if (callback_state != expected_state_) {
    throw std::runtime_error("OAuth callback state mismatch");
  }
  state_validated_ = true;
}

OAuthTokens OAuthFlow::ExchangeCode(const OAuthConfig& config,
                                    std::string_view code,
                                    std::string_view code_verifier,
                                    std::string_view redirect_uri,
                                    std::string_view resource) {
  {
    std::scoped_lock lock(mutex_);
    if (state_ != State::AwaitingCallback) {
      throw std::runtime_error(
          "OAuth authorization code exchange requested before authorization");
    }
    if (!state_validated_) {
      throw std::runtime_error(
          "OAuth authorization code exchange requires state validation");
    }
  }

  std::vector<std::pair<std::string, std::string>> form_fields = {
      {"grant_type", "authorization_code"},
      {"client_id", config.client_id},
      {"code", std::string(code)},
      {"code_verifier", std::string(code_verifier)},
      {"redirect_uri", std::string(redirect_uri)}};

  const std::string resolved_resource = ResolveResource(config, resource);
  if (!resolved_resource.empty()) {
    form_fields.emplace_back("resource", resolved_resource);
  }

  OAuthTokens tokens = PerformTokenRequest(config, form_fields);
  {
    std::scoped_lock lock(mutex_);
    state_ = State::Authorized;
  }
  return tokens;
}

OAuthTokens OAuthFlow::RefreshToken(const OAuthConfig& config,
                                    std::string_view refresh_token,
                                    std::string_view resource) {
  std::unique_lock lock(mutex_);
  if (refresh_in_progress_) {
    refresh_cv_.wait(lock, [this] { return !refresh_in_progress_; });
    if (refresh_error_.has_value()) {
      throw std::runtime_error(*refresh_error_);
    }
    return *refresh_result_;
  }
  refresh_in_progress_ = true;
  refresh_result_.reset();
  refresh_error_.reset();
  lock.unlock();

  std::vector<std::pair<std::string, std::string>> form_fields = {
      {"grant_type", "refresh_token"},
      {"client_id", config.client_id},
      {"refresh_token", std::string(refresh_token)}};

  const std::string resolved_resource = ResolveResource(config, resource);
  if (!resolved_resource.empty()) {
    form_fields.emplace_back("resource", resolved_resource);
  }

  try {
    OAuthTokens tokens = PerformTokenRequest(config, form_fields);
    {
      std::scoped_lock completion_lock(mutex_);
      state_ = State::Authorized;
      refresh_result_ = tokens;
      refresh_in_progress_ = false;
    }
    refresh_cv_.notify_all();
    return tokens;
  } catch (const std::exception& error) {
    {
      std::scoped_lock completion_lock(mutex_);
      refresh_error_ = error.what();
      refresh_in_progress_ = false;
    }
    refresh_cv_.notify_all();
    throw std::runtime_error(std::string("OAuth refresh failed: ") +
                             error.what());
  }
}

OAuthTokens OAuthFlow::PerformTokenRequest(
    const OAuthConfig& config,
    const std::vector<std::pair<std::string, std::string>>& form_fields) {
  const HttpResponse response = PostForm(config.token_url, form_fields);
  if (response.status_code >= 400) {
    throw std::runtime_error("OAuth token endpoint returned HTTP " +
                             std::to_string(response.status_code));
  }
  return ParseTokens(response.body);
}

}  // namespace yac::mcp::oauth
