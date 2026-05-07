#pragma once

#include "chat/prompt_library.hpp"
#include "chat/types.hpp"

#include <filesystem>

namespace yac::chat {

// Aggregated result returned by LoadConfig — bundles the parsed ChatConfig
// (with its issues) alongside the predefined prompt library (with its
// issues) so callers do not have to invoke the two loaders separately and
// merge their outputs by hand.
struct LoadConfigResult {
  ChatConfigResult chat;
  PromptLibraryResult prompt_library;
};

// Single entry point for loading every piece of YAC configuration:
//   1. Reads ~/.yac/settings.toml and overlays YAC_* env var overrides
//      (delegated to LoadChatConfigResultFrom).
//   2. Loads ~/.yac/prompts/*.toml and seeds the default init/review
//      prompts when missing (delegated to LoadPromptLibrary).
//
// Behavior is the physical concatenation of the two loaders' behavior —
// schema, env-var precedence, default-prompt seeding, and issue reporting
// are unchanged from calling them directly. The aggregate result keeps the
// per-source ChatConfigResult and PromptLibraryResult intact so callers can
// continue to inspect issues separately when they care to.
[[nodiscard]] LoadConfigResult LoadConfig(
    const std::filesystem::path& settings_path,
    const std::filesystem::path& prompts_dir);

// Convenience overload — resolves ~/.yac/settings.toml and ~/.yac/prompts
// internally via config_paths.hpp. Equivalent to invoking
// LoadChatConfigResult() + LoadPromptLibrary(true) in sequence.
[[nodiscard]] LoadConfigResult LoadConfig();

}  // namespace yac::chat
