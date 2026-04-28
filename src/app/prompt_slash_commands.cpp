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

bool IsProtectedCommand(const std::string& name) {
  for (const auto* protected_name : {"task"}) {
    if (name == protected_name) {
      return true;
    }
  }
  return false;
}

void AddPromptInfo(std::vector<chat::ConfigIssue>& issues, std::string message,
                   std::string detail) {
  issues.push_back({.severity = chat::ConfigIssueSeverity::Info,
                    .message = std::move(message),
                    .detail = std::move(detail)});
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
      if (IsProtectedCommand(prompt.name)) {
        AddPromptWarning(issues, "Skipped prompt command /" + prompt.name,
                         "This built-in command cannot be overridden.");
        continue;
      }
      registry.Undefine(prompt.name);
      AddPromptInfo(issues, "Overriding built-in /" + prompt.name,
                    "Replaced with prompt from ~/.yac/prompts/.");
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
