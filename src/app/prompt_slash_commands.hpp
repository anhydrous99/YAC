#pragma once

#include "chat/prompt_library.hpp"
#include "presentation/slash_command_registry.hpp"

#include <functional>
#include <string>
#include <vector>

namespace yac::app {

using PromptSubmitCallback = std::function<void(std::string)>;

void RegisterPromptSlashCommands(
    presentation::SlashCommandRegistry& registry,
    const std::vector<chat::PromptDefinition>& prompts,
    PromptSubmitCallback submit_prompt, std::vector<chat::ConfigIssue>& issues);

}  // namespace yac::app
