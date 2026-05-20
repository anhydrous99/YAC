#include "provider/openai_auth_flow.hpp"

#include "mcp/json_helpers.hpp"
#include "mcp/oauth/browser_launcher.hpp"
#include "mcp/oauth/loopback_callback.hpp"
#include "mcp/oauth/pkce.hpp"

#include <curl/curl.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace yac::provider {
namespace {

using Json = nlohmann::json;

constexpr std::string_view kAuthorizePath = "/oauth/authorize";
constexpr std::string_view kTokenPath = "/oauth/token";
constexpr std::string_view kClientId = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr std::string_view kScope = "openid profile email offline_access";
constexpr std::string_view kOriginator = "yac";
constexpr auto kRefreshSkew = std::chrono::seconds(120);

struct HttpResponse {
  long status_code = 0;
  std::string body;
};

[[nodiscard]] std::size_t WriteCallback(char* ptr, std::size_t size,
                                        std::size_t nmemb, void* userdata) {
  const std::size_t bytes = size * nmemb;
  auto* body = static_cast<std::string*>(userdata);
  body->append(ptr, bytes);
  return bytes;
}

[[nodiscard]] std::string JoinUrl(std::string_view base,
                                  std::string_view path) {
  std::string joined(base);
  while (!joined.empty() && joined.back() == '/') {
    joined.pop_back();
  }
  joined += path;
  return joined;
}

[[nodiscard]] std::string UrlEncode(std::string_view value) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }
  const auto cleanup = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup)> handle(curl, cleanup);

  char* escaped =
      curl_easy_escape(curl, value.data(), static_cast<int>(value.size()));
  if (escaped == nullptr) {
    throw std::runtime_error("curl_easy_escape failed");
  }
  const auto cleanup_escaped = [](char* value_ptr) { curl_free(value_ptr); };
  std::unique_ptr<char, decltype(cleanup_escaped)> escaped_handle(
      escaped, cleanup_escaped);
  return escaped;
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
  const auto cleanup = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup)> handle(curl, cleanup);

  const std::string payload = BuildFormBody(form_fields);
  std::string response_body;

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

[[nodiscard]] int DecodeBase64UrlChar(unsigned char value) {
  if (value >= 'A' && value <= 'Z') {
    return value - 'A';
  }
  if (value >= 'a' && value <= 'z') {
    return value - 'a' + 26;
  }
  if (value >= '0' && value <= '9') {
    return value - '0' + 52;
  }
  if (value == '-') {
    return 62;
  }
  if (value == '_') {
    return 63;
  }
  return -1;
}

[[nodiscard]] std::optional<std::string> Base64UrlDecode(
    std::string_view input) {
  std::string decoded;
  decoded.reserve((input.size() * 3) / 4);
  int buffer = 0;
  int bits = 0;
  for (const unsigned char value : input) {
    if (value == '=') {
      break;
    }
    const int index = DecodeBase64UrlChar(value);
    if (index < 0) {
      return std::nullopt;
    }
    buffer = (buffer << 6) | index;
    bits += 6;
    while (bits >= 8) {
      bits -= 8;
      decoded.push_back(static_cast<char>((buffer >> bits) & 0xff));
    }
  }
  return decoded;
}

[[nodiscard]] std::optional<Json> ParseJwtPayload(std::string_view token) {
  const std::size_t first_dot = token.find('.');
  if (first_dot == std::string_view::npos) {
    return std::nullopt;
  }
  const std::size_t second_dot = token.find('.', first_dot + 1);
  if (second_dot == std::string_view::npos) {
    return std::nullopt;
  }
  const auto payload =
      Base64UrlDecode(token.substr(first_dot + 1, second_dot - first_dot - 1));
  if (!payload.has_value()) {
    return std::nullopt;
  }
  try {
    return Json::parse(*payload);
  } catch (const std::exception& error) {
    (void)error;
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<std::string> ExtractAccountIdFromPayload(
    const Json& payload) {
  if (const auto it = payload.find("chatgpt_account_id");
      it != payload.end() && it->is_string()) {
    return it->get<std::string>();
  }
  if (const auto it =
          payload.find("https://api.openai.com/auth.chatgpt_account_id");
      it != payload.end() && it->is_string()) {
    return it->get<std::string>();
  }
  if (const auto orgs = payload.find("organizations");
      orgs != payload.end() && orgs->is_array() && !orgs->empty() &&
      (*orgs)[0].is_object()) {
    if (const auto org_id = (*orgs)[0].find("id");
        org_id != (*orgs)[0].end() && org_id->is_string()) {
      return org_id->get<std::string>();
    }
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::string> ExtractAccountId(
    const std::optional<std::string>& id_token, std::string_view access_token) {
  if (id_token.has_value()) {
    if (const auto id_payload = ParseJwtPayload(*id_token);
        id_payload.has_value()) {
      if (const auto account_id = ExtractAccountIdFromPayload(*id_payload);
          account_id.has_value()) {
        return account_id;
      }
    }
  }
  if (const auto access_payload = ParseJwtPayload(access_token);
      access_payload.has_value()) {
    return ExtractAccountIdFromPayload(*access_payload);
  }
  return std::nullopt;
}

[[nodiscard]] OpenAiAuthFlow::TokenResponse ParseTokenResponse(
    std::string_view body,
    std::function<std::chrono::system_clock::time_point()> clock) {
  const Json json = ::yac::mcp::ParseJsonOrThrow(body, "OpenAI OAuth token");
  if (!json.contains("access_token") || !json.at("access_token").is_string()) {
    throw std::runtime_error(
        "OpenAI OAuth token response missing access_token");
  }

  OpenAiAuthFlow::TokenResponse response;
  response.access_token = json.at("access_token").get<std::string>();
  if (const auto it = json.find("refresh_token");
      it != json.end() && it->is_string()) {
    response.refresh_token = it->get<std::string>();
  }
  if (const auto it = json.find("id_token");
      it != json.end() && it->is_string()) {
    response.id_token = it->get<std::string>();
  }
  if (const auto it = json.find("expires_in");
      it != json.end() && it->is_number_integer()) {
    response.expires_at = clock() + std::chrono::seconds(it->get<int>());
  }
  return response;
}

[[nodiscard]] std::string BuildTokenErrorMessage(long status_code,
                                                 std::string_view body,
                                                 bool is_refresh) {
  try {
    const Json json =
        ::yac::mcp::ParseJsonOrThrow(body, "OpenAI OAuth error response");
    const std::string error = json.value("error", std::string());
    const std::string description =
        json.value("error_description", std::string());
    if (is_refresh && error == "invalid_grant") {
      std::string message =
          "OpenAI refresh token was rejected or revoked. Sign in again.";
      if (!description.empty()) {
        message += " Token endpoint said: " + description;
      }
      return message;
    }
    if (!error.empty() || !description.empty()) {
      std::string message = "OpenAI OAuth token request failed";
      if (!error.empty()) {
        message += " (" + error + ")";
      }
      if (!description.empty()) {
        message += ": " + description;
      }
      return message;
    }
  } catch (const std::exception& error) {
    (void)error;
  }
  return "OpenAI OAuth token endpoint returned HTTP " +
         std::to_string(status_code);
}

[[nodiscard]] HttpResponse RequestTokens(
    std::string_view token_url,
    const std::vector<std::pair<std::string, std::string>>& fields) {
  const HttpResponse response = PostForm(token_url, fields);
  if (response.status_code >= 400) {
    throw std::runtime_error(
        BuildTokenErrorMessage(response.status_code, response.body,
                               fields.front().second == "refresh_token"));
  }
  return response;
}

[[nodiscard]] bool NeedsRefresh(const OpenAiOAuthAuth& auth,
                                std::chrono::system_clock::time_point now) {
  if (auth.access_token.empty()) {
    return true;
  }
  if (!auth.expires_at.has_value()) {
    return true;
  }
  return *auth.expires_at - kRefreshSkew <= now;
}

}  // namespace

OpenAiAuthFlow::OpenAiAuthFlow() : OpenAiAuthFlow(Dependencies{}) {}

OpenAiAuthFlow::OpenAiAuthFlow(Dependencies dependencies)
    : dependencies_(std::move(dependencies)) {
  if (!dependencies_.browser_launcher) {
    dependencies_.browser_launcher = yac::mcp::oauth::LaunchBrowser;
  }
  if (!dependencies_.clock) {
    dependencies_.clock = [] { return std::chrono::system_clock::now(); };
  }
  if (!dependencies_.code_verifier_generator) {
    dependencies_.code_verifier_generator =
        yac::mcp::oauth::GenerateCodeVerifier;
  }
  if (!dependencies_.code_challenge_deriver) {
    dependencies_.code_challenge_deriver = yac::mcp::oauth::DeriveCodeChallenge;
  }
  if (!dependencies_.state_generator) {
    dependencies_.state_generator = yac::mcp::oauth::GenerateCodeVerifier;
  }
  if (!dependencies_.auth_store) {
    dependencies_.auth_store = std::make_shared<OpenAiAuthStore>();
  }
}

std::optional<StoredOpenAiAuth> OpenAiAuthFlow::LoadStoredAuth() const {
  return dependencies_.auth_store->Load();
}

OpenAiOAuthAuth OpenAiAuthFlow::RunBrowserAuthorization(
    const OpenAiAuthorizationObserver& observer, std::stop_token stop_token) {
  yac::mcp::oauth::LoopbackCallbackServer server;
  const std::string redirect_uri = server.RedirectUri();
  const std::string code_verifier = dependencies_.code_verifier_generator();
  const std::string code_challenge =
      dependencies_.code_challenge_deriver(code_verifier);
  const std::string state = dependencies_.state_generator();
  const std::string authorization_url =
      BuildAuthorizationUrl(redirect_uri, code_challenge, state);
  const bool browser_launched =
      dependencies_.browser_launcher(authorization_url);

  if (observer) {
    observer(OpenAiAuthorizationNotice{.authorization_url = authorization_url,
                                       .redirect_uri = redirect_uri,
                                       .browser_launched = browser_launched});
  }

  const auto callback = server.RunUntilCallback(stop_token);
  if (!callback.has_value()) {
    throw std::runtime_error("OpenAI browser OAuth did not receive a callback");
  }
  ValidateCallbackState(callback->second, state);
  const TokenResponse response =
      ExchangeAuthorizationCode(callback->first, code_verifier, redirect_uri);
  return PersistTokenResponse(OpenAiOAuthAuth{}, response);
}

OpenAiOAuthAuth OpenAiAuthFlow::RefreshIfNeeded(const OpenAiOAuthAuth& auth) {
  const auto now = dependencies_.clock();
  if (!NeedsRefresh(auth, now)) {
    return auth;
  }

  std::scoped_lock lock(refresh_mutex_);
  const auto stored = dependencies_.auth_store->Load();
  OpenAiOAuthAuth current = auth;
  if (stored.has_value()) {
    if (const auto* stored_oauth =
            std::get_if<OpenAiOAuthAuth>(&stored->auth)) {
      current = *stored_oauth;
      if (!NeedsRefresh(current, dependencies_.clock())) {
        return current;
      }
    }
  }

  return RefreshLocked(current);
}

OpenAiOAuthAuth OpenAiAuthFlow::Refresh(const OpenAiOAuthAuth& auth) {
  std::scoped_lock lock(refresh_mutex_);
  const auto stored = dependencies_.auth_store->Load();
  OpenAiOAuthAuth current = auth;
  if (stored.has_value()) {
    if (const auto* stored_oauth =
            std::get_if<OpenAiOAuthAuth>(&stored->auth)) {
      current = *stored_oauth;
    }
  }

  return RefreshLocked(current);
}

OpenAiOAuthAuth OpenAiAuthFlow::RefreshLocked(const OpenAiOAuthAuth& auth) {
  const OpenAiOAuthAuth& current = auth;
  const TokenResponse response = RefreshToken(current.refresh_token);
  return PersistTokenResponse(current, response);
}

std::string OpenAiAuthFlow::BuildAuthorizationUrl(
    std::string_view redirect_uri, std::string_view code_challenge,
    std::string_view state) const {
  const std::vector<std::pair<std::string, std::string>> query = {
      {"response_type", "code"},
      {"client_id", std::string(kClientId)},
      {"redirect_uri", std::string(redirect_uri)},
      {"scope", std::string(kScope)},
      {"code_challenge", std::string(code_challenge)},
      {"code_challenge_method", "S256"},
      {"id_token_add_organizations", "true"},
      {"codex_cli_simplified_flow", "true"},
      {"state", std::string(state)},
      {"originator", std::string(kOriginator)}};

  std::ostringstream url;
  url << JoinUrl(dependencies_.issuer_url, kAuthorizePath) << '?';
  for (std::size_t index = 0; index < query.size(); ++index) {
    if (index > 0) {
      url << '&';
    }
    url << UrlEncode(query[index].first) << '='
        << UrlEncode(query[index].second);
  }
  return url.str();
}

void OpenAiAuthFlow::ValidateCallbackState(std::string_view callback_state,
                                           std::string_view expected_state) {
  if (callback_state != expected_state) {
    throw std::runtime_error("OpenAI OAuth callback state mismatch");
  }
}

OpenAiAuthFlow::TokenResponse OpenAiAuthFlow::ExchangeAuthorizationCode(
    std::string_view code, std::string_view code_verifier,
    std::string_view redirect_uri) const {
  const HttpResponse response =
      RequestTokens(JoinUrl(dependencies_.issuer_url, kTokenPath),
                    {{"grant_type", "authorization_code"},
                     {"code", std::string(code)},
                     {"redirect_uri", std::string(redirect_uri)},
                     {"client_id", std::string(kClientId)},
                     {"code_verifier", std::string(code_verifier)}});
  return ParseTokenResponse(response.body, dependencies_.clock);
}

OpenAiAuthFlow::TokenResponse OpenAiAuthFlow::RefreshToken(
    std::string_view refresh_token) const {
  const HttpResponse response =
      RequestTokens(JoinUrl(dependencies_.issuer_url, kTokenPath),
                    {{"grant_type", "refresh_token"},
                     {"refresh_token", std::string(refresh_token)},
                     {"client_id", std::string(kClientId)}});
  return ParseTokenResponse(response.body, dependencies_.clock);
}

OpenAiOAuthAuth OpenAiAuthFlow::PersistTokenResponse(
    const OpenAiOAuthAuth& prior_auth, const TokenResponse& response) const {
  OpenAiOAuthAuth updated{
      .refresh_token =
          response.refresh_token.value_or(prior_auth.refresh_token),
      .access_token = response.access_token,
      .expires_at = response.expires_at,
      .account_id = ExtractAccountId(response.id_token, response.access_token),
      .enterprise_url = prior_auth.enterprise_url,
  };
  if (!updated.account_id.has_value()) {
    updated.account_id = prior_auth.account_id;
  }
  (void)dependencies_.auth_store->Save(updated);
  return updated;
}

}  // namespace yac::provider
