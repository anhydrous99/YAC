#pragma once

#include "chat/types.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace yac::chat {

struct PromptDefinition {
  std::string name;
  std::string description;
  std::string prompt;
};

struct PromptLibraryResult {
  std::vector<PromptDefinition> prompts;
  std::vector<ConfigIssue> issues;
};

[[nodiscard]] PromptLibraryResult LoadPromptLibrary(
    const std::filesystem::path& prompts_dir, bool seed_defaults);

[[nodiscard]] PromptLibraryResult LoadPromptLibrary(bool seed_defaults);

[[nodiscard]] std::string RenderPrompt(const PromptDefinition& prompt,
                                       const std::string& arguments);

[[nodiscard]] std::string RenderPrompt(const std::string& prompt,
                                       const std::string& arguments);

}  // namespace yac::chat
