#include "slash_command_registry.hpp"

#include <algorithm>
#include <utility>

namespace yac::presentation {

void SlashCommandRegistry::Define(std::string id, std::string name,
                                  std::string description,
                                  std::vector<std::string> aliases) {
  const auto index = commands_.size();
  commands_.resize(index + 1);
  auto& command = commands_[index];
  command.id = std::move(id);
  command.name = std::move(name);
  command.description = std::move(description);
  command.aliases = std::move(aliases);
  command.handler = std::nullopt;
  command.arguments_handler = std::nullopt;
}

void SlashCommandRegistry::SetHandler(const std::string& id,
                                      std::function<void()> handler) {
  for (auto& command : commands_) {
    if (command.id == id) {
      command.handler = std::move(handler);
      break;
    }
  }
}

void SlashCommandRegistry::SetArgumentsHandler(
    const std::string& id, std::function<void(std::string)> handler) {
  for (auto& command : commands_) {
    if (command.id == id) {
      command.arguments_handler = std::move(handler);
      break;
    }
  }
}

void SlashCommandRegistry::Register(SlashCommand command) {
  if (command.id.empty()) {
    command.id = command.name;
  }
  const auto index = commands_.size();
  commands_.resize(index + 1);
  commands_[index] = std::move(command);
}

bool SlashCommandRegistry::TryDispatch(const std::string& input) const {
  if (input.empty() || input.front() != '/') {
    return false;
  }

  auto name = ExtractCommandName(input);
  if (name.empty()) {
    return false;
  }

  for (const auto& command : commands_) {
    const auto alias_match =
        std::find(command.aliases.begin(), command.aliases.end(), name) !=
        command.aliases.end();

    if (command.name == name || alias_match) {
      if (command.arguments_handler) {
        auto args_start = name.length() + 1;
        while (args_start < input.size() &&
               (input[args_start] == ' ' || input[args_start] == '\t' ||
                input[args_start] == '\n' || input[args_start] == '\r' ||
                input[args_start] == '\f' || input[args_start] == '\v')) {
          ++args_start;
        }
        auto args = input.substr(args_start);
        command.arguments_handler.value()(std::move(args));
        return true;
      }
      const auto& handler = command.handler;
      if (handler) {
        handler.value()();
        return true;
      }
    }
  }

  return false;
}

const std::vector<SlashCommand>& SlashCommandRegistry::Commands() const {
  return commands_;
}

bool SlashCommandRegistry::Undefine(const std::string& name) {
  for (auto it = commands_.begin(); it != commands_.end(); ++it) {
    if (it->name == name) {
      commands_.erase(it);
      return true;
    }
    for (const auto& alias : it->aliases) {
      if (alias == name) {
        commands_.erase(it);
        return true;
      }
    }
  }
  return false;
}

std::string SlashCommandRegistry::ExtractCommandName(const std::string& input) {
  auto start = input.find_first_not_of('/');
  if (start == std::string::npos) {
    return {};
  }
  auto end = input.find(' ', start);
  return input.substr(start, end - start);
}

void RegisterBuiltinSlashCommands(SlashCommandRegistry& registry) {
  registry.Define("quit", "quit", "Exit the application", {"exit"});
  registry.Define("clear", "clear", "Clear the conversation");
  registry.Define("cancel", "cancel", "Cancel the active response");
  registry.Define("compact", "compact", "Compact conversation history");
  registry.Define("init", "init", "Scan repo and create/update AGENTS.md");
  registry.Define("help", "help", "Show shortcuts and setup status", {"?"});
}

}  // namespace yac::presentation
