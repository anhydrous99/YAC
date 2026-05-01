#include "app/prompt_slash_commands.hpp"

#include <algorithm>
#include <array>
#include <string_view>
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
  static constexpr std::array<std::string_view, 1> kProtectedNames{"task"};
  return std::ranges::any_of(kProtectedNames,
                             [&name](std::string_view p) { return name == p; });
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
      static_cast<void>(registry.Undefine(prompt.name));
      AddPromptInfo(issues, "Overriding built-in /" + prompt.name,
                    "Replaced with prompt from ~/.yac/prompts/.");
    }

    const auto id = "prompt:" + prompt.name;
    registry.Define(id, prompt.name, prompt.description);

    registry.SetHandler(
        id, [prompt, submit_prompt] {  // NOLINT(bugprone-exception-escape)
          try {
            submit_prompt(chat::RenderPrompt(prompt, ""));
          } catch (...) {  // best-effort
          }
        });
    registry.SetArgumentsHandler(
        id, [prompt, submit_prompt](  // NOLINT(bugprone-exception-escape)
                std::string arguments) {
          try {
            submit_prompt(chat::RenderPrompt(prompt, std::move(arguments)));
          } catch (...) {  // best-effort
          }
        });
  }
}

}  // namespace yac::app
