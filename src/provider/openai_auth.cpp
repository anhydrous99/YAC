#include "provider/openai_auth.hpp"

#include <chrono>
#include <cstdint>
#include <sstream>
#include <stdexcept>

namespace yac::provider {
namespace {

using Json = nlohmann::json;

std::string RequireString(const Json& json, std::string_view field_name) {
  const std::string key(field_name);
  if (!json.contains(key) || !json.at(key).is_string()) {
    throw std::runtime_error("OpenAiAuth: missing or invalid '" + key + "'");
  }
  return json.at(key).get<std::string>();
}

std::optional<std::string> ReadOptionalString(const Json& json,
                                              std::string_view field_name) {
  const std::string key(field_name);
  if (!json.contains(key) || json.at(key).is_null()) {
    return std::nullopt;
  }
  if (!json.at(key).is_string()) {
    throw std::runtime_error("OpenAiAuth: invalid '" + key + "'");
  }
  return json.at(key).get<std::string>();
}

std::optional<std::chrono::system_clock::time_point> ReadOptionalExpiry(
    const Json& json, std::string_view field_name) {
  const std::string key(field_name);
  if (!json.contains(key) || json.at(key).is_null()) {
    return std::nullopt;
  }
  if (!json.at(key).is_number_integer()) {
    throw std::runtime_error("OpenAiAuth: invalid '" + key + "'");
  }
  const auto seconds = json.at(key).get<std::int64_t>();
  return std::chrono::system_clock::time_point{std::chrono::seconds(seconds)};
}

std::int64_t ToUnixSeconds(std::chrono::system_clock::time_point time_point) {
  return std::chrono::duration_cast<std::chrono::seconds>(
             time_point.time_since_epoch())
      .count();
}

}  // namespace

std::string SerializeOpenAiAuth(const OpenAiAuth& auth) {
  Json json;
  if (const auto* api_key = std::get_if<OpenAiApiKeyAuth>(&auth)) {
    json = Json{{"type", "api"}, {"key", api_key->key}};
    if (api_key->metadata.has_value()) {
      json["metadata"] = *api_key->metadata;
    }
    return json.dump();
  }

  const auto& oauth = std::get<OpenAiOAuthAuth>(auth);
  json = Json{{"type", "oauth"},
              {"refresh", oauth.refresh_token},
              {"access", oauth.access_token}};
  if (oauth.expires_at.has_value()) {
    json["expires"] = ToUnixSeconds(*oauth.expires_at);
  }
  if (oauth.account_id.has_value()) {
    json["accountId"] = *oauth.account_id;
  }
  if (oauth.enterprise_url.has_value()) {
    json["enterpriseUrl"] = *oauth.enterprise_url;
  }
  return json.dump();
}

OpenAiAuth ParseOpenAiAuth(std::string_view json_text) {
  const Json json = Json::parse(json_text);
  if (!json.is_object()) {
    throw std::runtime_error("OpenAiAuth: expected JSON object");
  }

  const std::string type = RequireString(json, "type");
  if (type == "api") {
    OpenAiApiKeyAuth auth{.key = RequireString(json, "key")};
    if (json.contains("metadata") && !json.at("metadata").is_null()) {
      auth.metadata = json.at("metadata");
    }
    return auth;
  }
  if (type == "oauth") {
    return OpenAiOAuthAuth{
        .refresh_token = RequireString(json, "refresh"),
        .access_token = RequireString(json, "access"),
        .expires_at = ReadOptionalExpiry(json, "expires"),
        .account_id = ReadOptionalString(json, "accountId"),
        .enterprise_url = ReadOptionalString(json, "enterpriseUrl"),
    };
  }

  throw std::runtime_error("OpenAiAuth: unsupported type '" + type + "'");
}

bool IsOpenAiOAuthExpired(const OpenAiOAuthAuth& auth,
                          std::chrono::system_clock::time_point now) {
  return auth.expires_at.has_value() && *auth.expires_at <= now;
}

bool HasUsableOpenAiOAuthAccessToken(
    const OpenAiOAuthAuth& auth, std::chrono::system_clock::time_point now) {
  return !auth.access_token.empty() && !IsOpenAiOAuthExpired(auth, now);
}

std::string_view OpenAiAuthTypeLabel(const OpenAiAuth& auth) {
  return std::holds_alternative<OpenAiApiKeyAuth>(auth) ? "OpenAI API key"
                                                        : "OpenAI OAuth";
}

std::string_view OpenAiAuthStorageSourceLabel(OpenAiAuthStorageSource source) {
  switch (source) {
    case OpenAiAuthStorageSource::StateStore:
      return "SQLite state store";
    case OpenAiAuthStorageSource::Keychain:
      return "OS keychain";
    case OpenAiAuthStorageSource::File:
      return "provider auth file";
  }
  return "unknown";
}

std::string RedactOpenAiSecret(std::string_view secret) {
  if (secret.empty()) {
    return "[REDACTED]";
  }
  if (secret.size() <= 4) {
    return "****";
  }
  if (secret.size() <= 8) {
    return std::string(secret.substr(0, 2)) + "...";
  }
  return std::string(secret.substr(0, 4)) + "..." +
         std::string(secret.substr(secret.size() - 4));
}

std::string DescribeOpenAiAuth(const OpenAiAuth& auth) {
  std::ostringstream stream;
  stream << OpenAiAuthTypeLabel(auth);
  if (const auto* api_key = std::get_if<OpenAiApiKeyAuth>(&auth)) {
    stream << " (key=" << RedactOpenAiSecret(api_key->key) << ")";
    return stream.str();
  }

  const auto& oauth = std::get<OpenAiOAuthAuth>(auth);
  stream << " (access=" << RedactOpenAiSecret(oauth.access_token)
         << ", refresh=" << RedactOpenAiSecret(oauth.refresh_token);
  if (oauth.expires_at.has_value()) {
    stream << ", expires=" << ToUnixSeconds(*oauth.expires_at);
  }
  stream << ")";
  return stream.str();
}

}  // namespace yac::provider
