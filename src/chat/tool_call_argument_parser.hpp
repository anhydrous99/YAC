#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace yac::chat {

[[nodiscard]] std::optional<std::string> ExtractStringFieldPartial(
    std::string_view partial_json, std::string_view key);

}  // namespace yac::chat
