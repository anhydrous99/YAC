#include "provider/openai_auth_flow.hpp"

#include "mcp/json_helpers.hpp"
#include "mcp/oauth/browser_launcher.hpp"
#include "mcp/oauth/pkce.hpp"
#include "provider/openai_auth_jwt.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstring>
#include <curl/curl.h>
#include <memory>
#include <netinet/in.h>
#include <nlohmann/json.hpp>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace yac::provider {
namespace {

using Json = nlohmann::json;

constexpr std::string_view kAuthorizePath = "/oauth/authorize";
constexpr std::string_view kTokenPath = "/oauth/token";
constexpr std::string_view kDeviceStartPath =
    "/api/accounts/deviceauth/usercode";
constexpr std::string_view kDeviceTokenPath = "/api/accounts/deviceauth/token";
constexpr std::string_view kClientId = "app_EMoamEEZ73f0CkXaXp7hrann";
constexpr std::string_view kScope = "openid profile email offline_access";
constexpr std::string_view kOriginator = "opencode";
constexpr std::string_view kBrowserRedirectUri =
    "http://localhost:1455/auth/callback";
constexpr std::string_view kDeviceRedirectUri =
    "https://auth.openai.com/deviceauth/callback";
constexpr std::string_view kDeviceVerificationUrl =
    "https://auth.openai.com/codex/device";
constexpr unsigned short kBrowserCallbackPort = 1455;
constexpr std::string_view kCallbackPortUnavailablePrefix =
    "OpenAI OAuth callback port 1455 is unavailable; free the port and retry "
    "or run yac auth openai login --device";
constexpr auto kRefreshSkew = std::chrono::seconds(120);
constexpr auto kDevicePollSafetyMargin = std::chrono::milliseconds(3000);

#ifndef YAC_VERSION
#define YAC_VERSION "dev"
#endif

struct HttpResponse {
  long status_code = 0;
  std::string body;
};

struct DeviceStartResponse {
  std::string device_auth_id;
  std::string user_code;
  std::chrono::milliseconds interval{0};
};

struct DevicePollResponse {
  std::optional<std::string> authorization_code;
  std::optional<std::string> code_verifier;
  std::optional<std::string> status;
  std::optional<std::chrono::milliseconds> interval;
};

[[nodiscard]] std::string BuildHttpResponse(int status, std::string_view reason,
                                            std::string_view body) {
  return std::string("HTTP/1.1 ") + std::to_string(status) + " " +
         std::string(reason) +
         "\r\nContent-Type: text/html; charset=utf-8\r\n"
         "Connection: close\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

[[nodiscard]] std::optional<std::string> ParseQueryParam(std::string_view query,
                                                         std::string_view key) {
  std::size_t pos = 0;
  while (pos <= query.size()) {
    const std::size_t amp = query.find('&', pos);
    const std::string_view segment =
        query.substr(pos, amp == std::string_view::npos ? std::string_view::npos
                                                        : amp - pos);
    if (segment.size() > key.size() && segment.starts_with(key) &&
        segment[key.size()] == '=') {
      return std::string(segment.substr(key.size() + 1));
    }
    if (amp == std::string_view::npos) {
      break;
    }
    pos = amp + 1;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::pair<std::string, std::string>>
ParseCallbackPath(std::string_view path) {
  const std::size_t q = path.find('?');
  if (q == std::string_view::npos) {
    return std::nullopt;
  }
  if (path.substr(0, q) != "/auth/callback") {
    return std::nullopt;
  }
  const std::string_view query = path.substr(q + 1);
  auto code = ParseQueryParam(query, "code");
  auto state = ParseQueryParam(query, "state");
  if (!code.has_value() || !state.has_value()) {
    return std::nullopt;
  }
  return std::make_pair(std::move(*code), std::move(*state));
}

[[nodiscard]] std::string ParseRequestPath(std::string_view request) {
  const std::size_t line_end = request.find("\r\n");
  const std::string_view first_line = request.substr(
      0, line_end == std::string_view::npos ? request.size() : line_end);
  const std::size_t sp1 = first_line.find(' ');
  if (sp1 == std::string_view::npos) {
    return {};
  }
  const std::size_t sp2 = first_line.find(' ', sp1 + 1);
  if (sp2 == std::string_view::npos) {
    return std::string(first_line.substr(sp1 + 1));
  }
  return std::string(first_line.substr(sp1 + 1, sp2 - sp1 - 1));
}

class OpenAiFixedLoopbackCallbackServer {
 public:
  OpenAiFixedLoopbackCallbackServer()
      : listen_fd_(socket(AF_INET, SOCK_STREAM, 0)) {
    if (listen_fd_ < 0) {
      throw std::runtime_error(std::string(kCallbackPortUnavailablePrefix) +
                               ": socket() failed");
    }

    int enable = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) != 0) {
      Close();
      throw std::runtime_error(std::string(kCallbackPortUnavailablePrefix) +
                               ": setsockopt() failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(kBrowserCallbackPort);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) !=
        0) {
      const std::string reason = std::strerror(errno);
      Close();
      throw std::runtime_error(std::string(kCallbackPortUnavailablePrefix) +
                               ": " + reason);
    }
    if (listen(listen_fd_, 1) != 0) {
      const std::string reason = std::strerror(errno);
      Close();
      throw std::runtime_error(std::string(kCallbackPortUnavailablePrefix) +
                               ": " + reason);
    }
  }

  ~OpenAiFixedLoopbackCallbackServer() { Close(); }

  OpenAiFixedLoopbackCallbackServer(const OpenAiFixedLoopbackCallbackServer&) =
      delete;
  OpenAiFixedLoopbackCallbackServer& operator=(
      const OpenAiFixedLoopbackCallbackServer&) = delete;
  OpenAiFixedLoopbackCallbackServer(OpenAiFixedLoopbackCallbackServer&&) =
      delete;
  OpenAiFixedLoopbackCallbackServer& operator=(
      OpenAiFixedLoopbackCallbackServer&&) = delete;

  [[nodiscard]] std::string RedirectUri() const {
    return std::string(kBrowserRedirectUri);
  }

  [[nodiscard]] std::optional<std::pair<std::string, std::string>>
  RunUntilCallback(std::stop_token stop_token) {
    while (!stop_token.stop_requested()) {
      if (listen_fd_ < 0) {
        return std::nullopt;
      }

      fd_set read_fds;
      FD_ZERO(&read_fds);
      FD_SET(listen_fd_, &read_fds);

      timeval timeout{};
      timeout.tv_sec = 0;
      timeout.tv_usec = 100000;

      const int ready =
          select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
      if (ready < 0) {
        return std::nullopt;
      }
      if (ready == 0) {
        continue;
      }

      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);
      const int client_fd = accept(
          listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
      if (client_fd < 0) {
        return std::nullopt;
      }

      std::string buffer;
      std::array<char, 4096> chunk{};
      while (buffer.find("\r\n\r\n") == std::string::npos) {
        const ssize_t bytes = recv(client_fd, chunk.data(), chunk.size(), 0);
        if (bytes <= 0) {
          break;
        }
        buffer.append(chunk.data(), static_cast<std::size_t>(bytes));
      }

      auto result = ParseCallbackPath(ParseRequestPath(buffer));
      const std::string response =
          result.has_value()
              ? BuildHttpResponse(200, "OK",
                                  "<html><body><h1>Authorization "
                                  "successful</h1><p>You may close "
                                  "this tab.</p></body></html>")
              : BuildHttpResponse(400, "Bad Request", "Invalid callback.\r\n");
      send(client_fd, response.data(), response.size(), 0);
      close(client_fd);

      Close();
      return result;
    }
    return std::nullopt;
  }

 private:
  void Close() {
    if (listen_fd_ >= 0) {
      close(listen_fd_);
      listen_fd_ = -1;
    }
  }

  int listen_fd_ = -1;
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

[[nodiscard]] std::string BuildJsonBody(
    const std::vector<std::pair<std::string, std::string>>& fields) {
  Json json = Json::object();
  for (const auto& [key, value] : fields) {
    json[key] = value;
  }
  return json.dump();
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

[[nodiscard]] HttpResponse PostJson(
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>>& fields) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }
  const auto cleanup = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup)> handle(curl, cleanup);

  const std::string payload = BuildJsonBody(fields);
  std::string response_body;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, "Accept: application/json");
  const std::string user_agent =
      std::string("User-Agent: opencode/") + YAC_VERSION;
  headers = curl_slist_append(headers, user_agent.c_str());
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
                                                 bool include_description) {
  try {
    const Json json =
        ::yac::mcp::ParseJsonOrThrow(body, "OpenAI OAuth error response");
    const std::string error = json.value("error", std::string());
    const std::string description =
        json.value("error_description", std::string());
    if (include_description && error == "invalid_grant") {
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
      if (include_description && !description.empty()) {
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

[[nodiscard]] std::string BuildDeviceErrorMessage(long status_code,
                                                  std::string_view body,
                                                  std::string_view context) {
  try {
    const Json json =
        ::yac::mcp::ParseJsonOrThrow(body, "OpenAI device auth error response");
    const std::string error = json.value("error", std::string());
    if (!error.empty()) {
      return "OpenAI device auth " + std::string(context) + " failed (" +
             error + ")";
    }
  } catch (const std::exception& error) {
    (void)error;
  }
  return "OpenAI device auth " + std::string(context) +
         " endpoint returned HTTP " + std::to_string(status_code);
}

[[nodiscard]] HttpResponse RequestTokens(
    std::string_view token_url,
    const std::vector<std::pair<std::string, std::string>>& fields) {
  const HttpResponse response = PostForm(token_url, fields);
  if (response.status_code >= 400) {
    const bool is_refresh = fields.front().second == "refresh_token";
    throw std::runtime_error(BuildTokenErrorMessage(response.status_code,
                                                    response.body, is_refresh));
  }
  return response;
}

[[nodiscard]] HttpResponse RequestDeviceJson(
    std::string_view url,
    const std::vector<std::pair<std::string, std::string>>& fields,
    std::string_view context) {
  const HttpResponse response = PostJson(url, fields);
  if (response.status_code >= 400) {
    throw std::runtime_error(
        BuildDeviceErrorMessage(response.status_code, response.body, context));
  }
  return response;
}

[[nodiscard]] std::chrono::milliseconds ParseInterval(const Json& json,
                                                      std::string_view key) {
  if (const auto it = json.find(std::string(key)); it != json.end()) {
    if (it->is_number_integer()) {
      return std::chrono::seconds(it->get<int>());
    }
    if (it->is_number_float()) {
      return std::chrono::milliseconds(
          static_cast<int>(it->get<double>() * 1000.0));
    }
  }
  return std::chrono::milliseconds(0);
}

[[nodiscard]] DeviceStartResponse ParseDeviceStartResponse(
    std::string_view body) {
  const Json json =
      ::yac::mcp::ParseJsonOrThrow(body, "OpenAI device auth start response");
  if (!json.contains("device_auth_id") ||
      !json.at("device_auth_id").is_string()) {
    throw std::runtime_error(
        "OpenAI device auth start response missing device_auth_id");
  }
  if (!json.contains("user_code") || !json.at("user_code").is_string()) {
    throw std::runtime_error(
        "OpenAI device auth start response missing user_code");
  }
  return DeviceStartResponse{
      .device_auth_id = json.at("device_auth_id").get<std::string>(),
      .user_code = json.at("user_code").get<std::string>(),
      .interval = ParseInterval(json, "interval"),
  };
}

[[nodiscard]] DevicePollResponse ParseDevicePollResponse(
    std::string_view body) {
  const Json json =
      ::yac::mcp::ParseJsonOrThrow(body, "OpenAI device auth poll response");
  DevicePollResponse response;
  if (const auto it = json.find("authorization_code");
      it != json.end() && it->is_string()) {
    response.authorization_code = it->get<std::string>();
  }
  if (const auto it = json.find("code_verifier");
      it != json.end() && it->is_string()) {
    response.code_verifier = it->get<std::string>();
  }
  if (const auto it = json.find("status");
      it != json.end() && it->is_string()) {
    response.status = it->get<std::string>();
  }
  const auto interval = ParseInterval(json, "interval");
  if (interval.count() > 0) {
    response.interval = interval;
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
  if (!dependencies_.sleep_for) {
    dependencies_.sleep_for = [](std::chrono::milliseconds duration,
                                 std::stop_token stop_token) {
      const auto deadline = std::chrono::steady_clock::now() + duration;
      while (!stop_token.stop_requested() &&
             std::chrono::steady_clock::now() < deadline) {
        const auto remaining = deadline - std::chrono::steady_clock::now();
        std::this_thread::sleep_for(std::min<std::chrono::milliseconds>(
            std::chrono::duration_cast<std::chrono::milliseconds>(remaining),
            std::chrono::milliseconds(100)));
      }
      return !stop_token.stop_requested();
    };
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
  OpenAiFixedLoopbackCallbackServer server;
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

OpenAiOAuthAuth OpenAiAuthFlow::RunDeviceAuthorization(
    const OpenAiDeviceAuthorizationObserver& observer,
    std::stop_token stop_token) {
  const HttpResponse start_http =
      RequestDeviceJson(JoinUrl(dependencies_.issuer_url, kDeviceStartPath),
                        {{"client_id", std::string(kClientId)}}, "start");
  const DeviceStartResponse start = ParseDeviceStartResponse(start_http.body);

  if (observer) {
    observer(OpenAiDeviceAuthorizationNotice{
        .verification_url = std::string(kDeviceVerificationUrl),
        .user_code = start.user_code,
    });
  }

  std::chrono::milliseconds interval = start.interval;
  while (!stop_token.stop_requested()) {
    if (!dependencies_.sleep_for(interval + kDevicePollSafetyMargin,
                                 stop_token)) {
      break;
    }
    const HttpResponse poll_http =
        RequestDeviceJson(JoinUrl(dependencies_.issuer_url, kDeviceTokenPath),
                          {{"device_auth_id", start.device_auth_id},
                           {"user_code", start.user_code}},
                          "poll");
    const DevicePollResponse poll = ParseDevicePollResponse(poll_http.body);
    if (poll.interval.has_value()) {
      interval = *poll.interval;
    }
    if (poll.authorization_code.has_value() && poll.code_verifier.has_value()) {
      const TokenResponse response = ExchangeAuthorizationCode(
          *poll.authorization_code, *poll.code_verifier, kDeviceRedirectUri);
      return PersistTokenResponse(OpenAiOAuthAuth{}, response);
    }
    if (poll.status == "denied" || poll.status == "declined" ||
        poll.status == "rejected") {
      throw std::runtime_error(
          "OpenAI device auth was denied. Run yac auth openai login --device "
          "again to retry.");
    }
    if (poll.status == "failed") {
      throw std::runtime_error(
          "OpenAI device auth failed. Run yac auth openai login --device "
          "again to retry.");
    }
    if (poll.status == "expired" || poll.status == "timeout") {
      throw std::runtime_error(
          "OpenAI device auth expired before approval. Run yac auth openai "
          "login --device again to retry.");
    }
  }

  throw std::runtime_error(
      "OpenAI device auth stopped before approval. Run yac auth openai "
      "login --device again to retry.");
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
