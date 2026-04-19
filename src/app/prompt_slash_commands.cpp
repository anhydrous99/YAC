#include "app/prompt_slash_commands.hpp"

#include <utility>

namespace yac::app {
namespace {

bool CommandNameExists(const presentation::SlashCommandRegistry& registry,
                       const std::string& name) {
  for (const auto& command : registry.Commands()) {
    if (command.name == name) {
      return true;
    }
    for (const auto& alias : command.aliases) {
      if (alias == name) {
        return true;
      }
    }
  }
  return false;
}

void AddPromptWarning(std::vector<chat::ConfigIssue>& issues,
                      std::string message, std::string detail) {
  issues.push_back({.severity = chat::ConfigIssueSeverity::Warning,
                    .message = std::move(message),
                    .detail = std::move(detail)});
}

}  // namespace

void RegisterPromptSlashCommands(
    presentation::SlashCommandRegistry& registry,
    const std::vector<chat::PromptDefinition>& prompts,
    PromptSubmitCallback submit_prompt,
    std::vector<chat::ConfigIssue>& issues) {
  for (const auto& prompt : prompts) {
    if (CommandNameExists(registry, prompt.name)) {
      AddPromptWarning(issues, "Skipped prompt command /" + prompt.name,
                       "A built-in or earlier slash command already uses this "
                       "name.");
      continue;
    }

    const auto id = "prompt:" + prompt.name;
    registry.Define(id, prompt.name, prompt.description);

    registry.SetHandler(id, [prompt, submit_prompt] {
      submit_prompt(chat::RenderPrompt(prompt, ""));
    });
    registry.SetArgumentsHandler(
        id, [prompt, submit_prompt](std::string arguments) {
          submit_prompt(chat::RenderPrompt(prompt, arguments));
        });
  }
}

}  // namespace yac::app
