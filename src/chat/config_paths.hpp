#pragma once

#include <filesystem>
#include <optional>

namespace yac::chat {

// Resolves the current user's home directory. Uses $HOME first, then falls
// back to getpwuid(getuid())->pw_dir on POSIX. Returns std::nullopt if no
// home can be determined (unusual — typically means the process is running
// with a broken environment and no passwd entry).
[[nodiscard]] std::optional<std::filesystem::path> ResolveHomeDir();

// ~/.yac — the YAC per-user config directory.
[[nodiscard]] std::filesystem::path GetYacConfigDir(
    const std::filesystem::path& home);

// ~/.yac/settings.toml — the canonical settings file.
[[nodiscard]] std::filesystem::path GetSettingsPath(
    const std::filesystem::path& home);

// Convenience overloads that call ResolveHomeDir() internally. Throw
// std::runtime_error if the home directory cannot be resolved.
[[nodiscard]] std::filesystem::path GetYacConfigDir();
[[nodiscard]] std::filesystem::path GetSettingsPath();

}  // namespace yac::chat
