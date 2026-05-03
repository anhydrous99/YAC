#pragma once

#include "chat/types.hpp"

#include <filesystem>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace yac::chat {

struct McpServerFieldSet {
  bool command = false;
  bool args = false;
  bool url = false;
  bool enabled = false;
  bool api_key_env = false;
};

// Bitset tracking which fields a settings.toml file set explicitly. Used so
// that provider presets only fill in fields the user did not override.
struct ChatConfigFieldSet {
  bool provider_id = false;
  bool model = false;
  bool base_url = false;
  bool api_key_env = false;
  bool api_key = false;
  bool temperature = false;
  bool max_tool_rounds = false;
  bool context_window = false;
  bool system_prompt = false;
  bool workspace_root = false;
  bool lsp_clangd_command = false;
  bool lsp_clangd_args = false;
  bool theme_name = false;
  bool theme_density = false;
  std::unordered_map<std::string, McpServerFieldSet> mcp_servers;
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

// Persists a runtime theme selection by updating [theme].name in settings.toml.
// Creates the default settings file if it is missing. Malformed TOML or an
// invalid [theme] shape is left untouched and reported through `issues`.
[[nodiscard]] bool SaveThemeNameToSettingsToml(
    const std::filesystem::path& path, std::string_view theme_name,
    std::vector<ConfigIssue>& issues);

}  // namespace yac::chat
