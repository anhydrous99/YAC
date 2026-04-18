#pragma once

#include "chat/types.hpp"

#include <filesystem>

namespace yac::chat {

// Loads YAC's chat configuration from ~/.yac/settings.toml, overlays YAC_*
// shell env var overrides, resolves the API key, and validates values. If the
// settings file is missing, it is auto-created with a commented default
// template. Parse errors, invalid values, and a missing API key are reported
// through ChatConfigResult::issues rather than thrown.
[[nodiscard]] ChatConfigResult LoadChatConfigResult();
[[nodiscard]] ChatConfig LoadChatConfig();

// Lower-level entry point used by the unit tests: reads the TOML file at
// `settings_path`, overlays env vars, and (optionally) writes the default
// template if `create_if_missing` is true and the file is absent. Production
// callers should prefer LoadChatConfigResult(), which resolves the path via
// config_paths.hpp.
[[nodiscard]] ChatConfigResult LoadChatConfigResultFrom(
    const std::filesystem::path& settings_path, bool create_if_missing);

}  // namespace yac::chat
