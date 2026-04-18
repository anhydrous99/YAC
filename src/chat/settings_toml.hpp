#pragma once

#include "chat/types.hpp"

#include <filesystem>
#include <vector>

namespace yac::chat {

// Bitset tracking which fields a settings.toml file set explicitly. Used so
// that provider presets only fill in fields the user did not override.
struct ChatConfigFieldSet {
  bool provider_id = false;
  bool model = false;
  bool base_url = false;
  bool api_key_env = false;
  bool api_key = false;
  bool temperature = false;
  bool system_prompt = false;
  bool workspace_root = false;
  bool lsp_clangd_command = false;
  bool lsp_clangd_args = false;
};

// Parses settings.toml at the given path and overlays values onto `config`.
// Records parse errors, unexpected types, and out-of-range values as
// ConfigIssue entries. Missing files are treated as a no-op (returns an empty
// field set and pushes no issues). Malformed files produce an Error issue but
// never throw.
ChatConfigFieldSet LoadSettingsFromToml(const std::filesystem::path& path,
                                        ChatConfig& config,
                                        std::vector<ConfigIssue>& issues);

// Creates the parent directory (mode 0700 on POSIX) and writes the default
// commented template to `path` with mode 0600 on POSIX. Records any I/O
// failure as a Warning issue rather than throwing — startup should continue
// with built-in defaults.
void WriteDefaultSettingsToml(const std::filesystem::path& path,
                              std::vector<ConfigIssue>& issues);

}  // namespace yac::chat
