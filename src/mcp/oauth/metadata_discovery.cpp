#include "mcp/oauth/metadata_discovery.hpp"

#include <curl/curl.h>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
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

std::size_t WriteCallback(char* ptr, std::size_t size, std::size_t nmemb,
                          void* userdata) {
  const std::size_t bytes = size * nmemb;
  auto* body = static_cast<std::string*>(userdata);
  body->append(ptr, bytes);
  return bytes;
}

[[nodiscard]] HttpResponse HttpGet(std::string_view url) {
  CURL* curl = curl_easy_init();
  if (curl == nullptr) {
    throw std::runtime_error("curl_easy_init failed");
  }

  const auto cleanup_curl = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup_curl)> curl_handle(curl, cleanup_curl);

  std::string body;
  const std::string url_str(url);

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Accept: application/json");
  const auto cleanup_headers = [](curl_slist* list) {
    curl_slist_free_all(list);
  };
  std::unique_ptr<curl_slist, decltype(cleanup_headers)> header_handle(
      headers, cleanup_headers);

  curl_easy_setopt(curl, CURLOPT_URL, url_str.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 30000L);

  const CURLcode result = curl_easy_perform(curl);
  if (result != CURLE_OK) {
    throw std::runtime_error(curl_easy_strerror(result));
  }

  HttpResponse response;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status_code);
  response.body = std::move(body);
  return response;
}

[[nodiscard]] bool IsAllowedAsUrl(std::string_view url) {
  if (url.starts_with("https://")) {
    return true;
  }
  return url.starts_with("http://127.0.0.1") ||
         url.starts_with("http://localhost");
}

void ValidateAsUrl(std::string_view url) {
  if (!IsAllowedAsUrl(url)) {
    throw std::runtime_error(
        "Authorization server URL must use HTTPS or loopback http://: " +
        std::string(url));
  }
}

[[nodiscard]] std::string GetOrigin(std::string_view url) {
  constexpr std::string_view kSchemeDelim = "://";
  const auto scheme_end = url.find(kSchemeDelim);
  if (scheme_end == std::string_view::npos) {
    throw std::runtime_error("Invalid URL: missing scheme: " +
                             std::string(url));
  }
  const auto path_start = url.find('/', scheme_end + kSchemeDelim.size());
  if (path_start == std::string_view::npos) {
    return std::string(url);
  }
  return std::string(url.substr(0, path_start));
}

[[nodiscard]] std::optional<std::string> ParseResourceMetadataUrl(
    std::string_view header) {
  constexpr std::string_view kKey = "resource_metadata=\"";
  const auto pos = header.find(kKey);
  if (pos == std::string_view::npos) {
    return std::nullopt;
  }
  const auto start = pos + kKey.size();
  const auto end = header.find('"', start);
  if (end == std::string_view::npos) {
    return std::nullopt;
  }
  return std::string(header.substr(start, end - start));
}

[[nodiscard]] std::optional<std::string> TryFetchJson(std::string_view url) {
  const HttpResponse response = HttpGet(url);
  if (response.status_code < 200 || response.status_code >= 300) {
    return std::nullopt;
  }
  return response.body;
}

[[nodiscard]] std::string FetchJson(std::string_view url) {
  const HttpResponse response = HttpGet(url);
  if (response.status_code < 200 || response.status_code >= 300) {
    throw std::runtime_error("HTTP " + std::to_string(response.status_code) +
                             " fetching " + std::string(url));
  }
  return response.body;
}

[[nodiscard]] std::string ExtractAuthorizationServer(
    const std::string& resource_metadata_body) {
  Json json;
  try {
    json = Json::parse(resource_metadata_body);
  } catch (const std::exception& error) {
    throw std::runtime_error(std::string("Invalid resource metadata JSON: ") +
                             error.what());
  }

  if (!json.contains("authorization_servers") ||
      !json["authorization_servers"].is_array() ||
      json["authorization_servers"].empty()) {
    throw std::runtime_error(
        "Resource metadata missing 'authorization_servers'");
  }

  const auto& first = json["authorization_servers"][0];
  if (!first.is_string()) {
    throw std::runtime_error(
        "Resource metadata 'authorization_servers[0]' is not a string");
  }
  return first.get<std::string>();
}

[[nodiscard]] OAuthDiscoveryResult ParseAsMetadata(const std::string& body) {
  Json json;
  try {
    json = Json::parse(body);
  } catch (const std::exception& error) {
    throw std::runtime_error(std::string("Invalid AS metadata JSON: ") +
                             error.what());
  }

  if (!json.contains("authorization_endpoint") ||
      !json["authorization_endpoint"].is_string()) {
    throw std::runtime_error(
        "AS metadata missing required field: authorization_endpoint");
  }
  if (!json.contains("token_endpoint") || !json["token_endpoint"].is_string()) {
    throw std::runtime_error(
        "AS metadata missing required field: token_endpoint");
  }

  OAuthDiscoveryResult result;
  result.authorization_endpoint =
      json["authorization_endpoint"].get<std::string>();
  result.token_endpoint = json["token_endpoint"].get<std::string>();

  if (json.contains("code_challenge_methods_supported") &&
      json["code_challenge_methods_supported"].is_array()) {
    for (const auto& method : json["code_challenge_methods_supported"]) {
      if (method.is_string()) {
        result.code_challenge_methods_supported.push_back(
            method.get<std::string>());
      }
    }
  }

  bool has_s256 = false;
  for (const auto& method : result.code_challenge_methods_supported) {
    if (method == "S256") {
      has_s256 = true;
      break;
    }
  }
  if (!has_s256) {
    throw std::runtime_error(
        "Authorization server does not support PKCE S256; discovery refused");
  }

  return result;
}

}  // namespace

OAuthDiscoveryResult DiscoverAuthorizationServer(std::string_view as_url) {
  ValidateAsUrl(as_url);
  const std::string origin = GetOrigin(as_url);

  const std::string rfc8414_url =
      origin + "/.well-known/oauth-authorization-server";
  if (const auto body = TryFetchJson(rfc8414_url)) {
    return ParseAsMetadata(*body);
  }

  const std::string oidc_url = origin + "/.well-known/openid-configuration";
  if (const auto body = TryFetchJson(oidc_url)) {
    return ParseAsMetadata(*body);
  }

  throw std::runtime_error(
      "AS metadata not found at " + origin +
      " (tried /.well-known/oauth-authorization-server and "
      "/.well-known/openid-configuration)");
}

OAuthDiscoveryResult DiscoverProtectedResource(
    std::string_view mcp_endpoint, std::string_view www_authenticate_header) {
  if (!www_authenticate_header.empty()) {
    if (const auto resource_metadata_url =
            ParseResourceMetadataUrl(www_authenticate_header)) {
      const std::string body = FetchJson(*resource_metadata_url);
      const std::string as_url = ExtractAuthorizationServer(body);
      return DiscoverAuthorizationServer(as_url);
    }
  }

  const std::string endpoint_str(mcp_endpoint);

  const std::string fallback1 =
      endpoint_str + "/.well-known/oauth-protected-resource";
  if (const auto body1 = TryFetchJson(fallback1)) {
    const std::string as_url = ExtractAuthorizationServer(*body1);
    return DiscoverAuthorizationServer(as_url);
  }

  const std::string origin = GetOrigin(mcp_endpoint);
  const std::string fallback2 =
      origin + "/.well-known/oauth-protected-resource";
  if (fallback2 != fallback1) {
    if (const auto body2 = TryFetchJson(fallback2)) {
      const std::string as_url = ExtractAuthorizationServer(*body2);
      return DiscoverAuthorizationServer(as_url);
    }
  }

  throw std::runtime_error(
      "Protected resource metadata not found at any well-known endpoint");
}

}  // namespace yac::mcp::oauth
