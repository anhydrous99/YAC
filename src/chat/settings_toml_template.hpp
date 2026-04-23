#pragma once

#include <string_view>

namespace yac::chat {

inline constexpr std::string_view kDefaultSettingsToml = R"(# YAC configuration.
#
# This file was auto-generated on first launch. Edit any value and restart
# YAC to pick up the change. Shell environment variables named YAC_* override
# anything set here at startup, which is convenient for CI and per-shell
# experiments.
#
# Secrets: prefer exporting your API key in your shell (OPENAI_API_KEY,
# ZAI_API_KEY) instead of placing it in [provider].api_key below. The env-var
# path is the default and keeps your key out of a plaintext file in $HOME.

temperature = 0.7
# system_prompt = "You are a helpful assistant."
# workspace_root = "/path/to/workspace"   # defaults to the launch directory

[provider]
id          = "openai"                    # "openai" or "zai"
model       = "gpt-4o-mini"
base_url    = "https://api.openai.com/v1/"
api_key_env = "OPENAI_API_KEY"            # env var that holds the key
# api_key   = ""                          # optional; prefer the env var

# Z.ai preset example — uncomment to switch providers.
# Leaving model/base_url/api_key_env unset applies the built-in zai defaults
# (glm-5.1, https://api.z.ai/api/coding/paas/v4, ZAI_API_KEY).
# [provider]
# id = "zai"

[lsp.clangd]
command = "clangd"
args    = []

[theme]
# Active theme preset. Built-in: "opencode" (default), "catppuccin",
# "system". The "system" theme uses terminal default colors and
# disables OSC 11 background sync.
name = "opencode"

# Theme density: "comfortable" (default, normal spacing) or "compact"
# (tighter layout).
# density = "comfortable"

# Paint the terminal background to match the active theme's canvas
# color (via OSC 11) and restore the terminal default on exit. Has
# no effect when name = "system".
sync_terminal_background = true
)";

}  // namespace yac::chat
