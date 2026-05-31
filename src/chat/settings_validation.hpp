#pragma once

#include "chat/settings_registry.hpp"

#include <string>

namespace yac::chat {

[[nodiscard]] std::string TomlValidationDetail(
    const SettingValidationMetadata& validation);
[[nodiscard]] std::string EnvValidationDetail(
    const SettingValidationMetadata& validation);

}  // namespace yac::chat
