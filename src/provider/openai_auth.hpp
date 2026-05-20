#pragma once

#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace yac::provider {

struct OpenAiApiKeyAuth {
  std::string key;
  std::optional<nlohmann::json> metadata;
};

struct OpenAiOAuthAuth {
  std::string refresh_token;
  std::string access_token;
  std::optional<std::chrono::system_clock::time_point> expires_at;
  std::optional<std::string> account_id;
  std::optional<std::string> enterprise_url;
};

using OpenAiAuth = std::variant<OpenAiApiKeyAuth, OpenAiOAuthAuth>;

enum class OpenAiAuthStorageSource {
  Keychain,
  File,
};

struct StoredOpenAiAuth {
  OpenAiAuth auth;
  OpenAiAuthStorageSource source;
};

[[nodiscard]] std::string SerializeOpenAiAuth(const OpenAiAuth& auth);
[[nodiscard]] OpenAiAuth ParseOpenAiAuth(std::string_view json_text);

[[nodiscard]] bool IsOpenAiOAuthExpired(
    const OpenAiOAuthAuth& auth, std::chrono::system_clock::time_point now =
                                     std::chrono::system_clock::now());

[[nodiscard]] bool HasUsableOpenAiOAuthAccessToken(
    const OpenAiOAuthAuth& auth, std::chrono::system_clock::time_point now =
                                     std::chrono::system_clock::now());

[[nodiscard]] std::string_view OpenAiAuthTypeLabel(const OpenAiAuth& auth);
[[nodiscard]] std::string_view OpenAiAuthStorageSourceLabel(
    OpenAiAuthStorageSource source);

[[nodiscard]] std::string RedactOpenAiSecret(std::string_view secret);
[[nodiscard]] std::string DescribeOpenAiAuth(const OpenAiAuth& auth);

}  // namespace yac::provider
