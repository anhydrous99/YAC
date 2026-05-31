#pragma once

#include <nlohmann/json.hpp>
#include <optional>
#include <string>
#include <string_view>

namespace yac::provider {

[[nodiscard]] std::optional<std::string> Base64UrlDecode(
    std::string_view input);
[[nodiscard]] std::optional<nlohmann::json> ParseJwtPayload(
    std::string_view token);
[[nodiscard]] std::optional<std::string> ExtractAccountIdFromPayload(
    const nlohmann::json& payload);
[[nodiscard]] std::optional<std::string> ExtractAccountId(
    const std::optional<std::string>& id_token, std::string_view access_token);

}  // namespace yac::provider
