#pragma once

#include "chat/settings_registry.hpp"

#include <string>

namespace yac::chat {

inline const std::string kDefaultSettingsToml =
    GenerateDefaultSettingsTomlTemplate();

}  // namespace yac::chat
