#include "app/prompt_slash_commands.hpp"
#include "chat/prompt_library.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/slash_command_registry.hpp"

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/event.hpp>

using yac::app::RegisterPromptSlashCommands;
using yac::chat::ConfigIssue;
using yac::chat::PromptDefinition;
using yac::presentation::ChatUI;
using yac::presentation::RegisterBuiltinSlashCommands;
using yac::presentation::SlashCommandRegistry;

namespace {

void TypeText(const ftxui::Component& component, const std::string& text) {
  for (char ch : text) {
    REQUIRE(component->OnEvent(ftxui::Event::Character(ch)));
  }
}

bool HasIssueContaining(const std::vector<ConfigIssue>& issues,
                        const std::string& text) {
  for (const auto& issue : issues) {
    if (issue.message.find(text) != std::string::npos ||
        issue.detail.find(text) != std::string::npos) {
      return true;
    }
  }
  return false;
}

}  // namespace

TEST_CASE("RegisterPromptSlashCommands registers init and review commands") {
  SlashCommandRegistry registry;
  std::vector<PromptDefinition> prompts = {
      {.name = "init", .description = "Init", .prompt = "Initialize"},
      {.name = "review", .description = "Review", .prompt = "Review"},
  };
  std::vector<std::string> submitted;
  std::vector<ConfigIssue> issues;

  RegisterPromptSlashCommands(
      registry, prompts,
      [&submitted](std::string prompt) {
        submitted.push_back(std::move(prompt));
      },
      issues);

  REQUIRE(issues.empty());
  REQUIRE(registry.TryDispatch("/init"));
  REQUIRE(registry.TryDispatch("/review"));
  REQUIRE(submitted == std::vector<std::string>{"Initialize", "Review"});
}

TEST_CASE("RegisterPromptSlashCommands passes slash command arguments") {
  SlashCommandRegistry registry;
  std::vector<PromptDefinition> prompts = {
      {.name = "review",
       .description = "Review",
       .prompt = "Review target: $ARGUMENTS"},
  };
  std::vector<std::string> submitted;
  std::vector<ConfigIssue> issues;

  RegisterPromptSlashCommands(
      registry, prompts,
      [&submitted](std::string prompt) {
        submitted.push_back(std::move(prompt));
      },
      issues);

  REQUIRE(registry.TryDispatch("/review main"));
  REQUIRE(submitted == std::vector<std::string>{"Review target: main"});
}

TEST_CASE("Prompt slash commands can execute from the slash menu") {
  ChatUI ui;
  SlashCommandRegistry registry;
  std::vector<PromptDefinition> prompts = {
      {.name = "init", .description = "Init", .prompt = "Initialize repo"},
  };
  std::vector<std::string> submitted;
  std::vector<ConfigIssue> issues;
  RegisterPromptSlashCommands(
      registry, prompts,
      [&submitted](std::string prompt) {
        submitted.push_back(std::move(prompt));
      },
      issues);
  ui.SetSlashCommands(std::move(registry));
  auto component = ui.Build();

  TypeText(component, "/in");
  REQUIRE(component->OnEvent(ftxui::Event::Return));

  REQUIRE(submitted == std::vector<std::string>{"Initialize repo"});
}

TEST_CASE("RegisterPromptSlashCommands lets built-ins win prompt conflicts") {
  SlashCommandRegistry registry;
  RegisterBuiltinSlashCommands(registry);
  int clear_count = 0;
  registry.SetHandler("clear", [&clear_count] { ++clear_count; });
  std::vector<PromptDefinition> prompts = {
      {.name = "clear", .description = "Prompt clear", .prompt = "Prompt"},
      {.name = "exit", .description = "Prompt exit", .prompt = "Prompt"},
  };
  std::vector<std::string> submitted;
  std::vector<ConfigIssue> issues;

  RegisterPromptSlashCommands(
      registry, prompts,
      [&submitted](std::string prompt) {
        submitted.push_back(std::move(prompt));
      },
      issues);

  REQUIRE(issues.size() == 2);
  REQUIRE(HasIssueContaining(issues, "/clear"));
  REQUIRE(HasIssueContaining(issues, "/exit"));
  REQUIRE(registry.TryDispatch("/clear"));
  REQUIRE(clear_count == 1);
  REQUIRE(submitted.empty());
}
