#include "slash_command_registry.hpp"

#include <algorithm>
#include <utility>

namespace yac::presentation {

void SlashCommandRegistry::Define(std::string id, std::string name,
                                  std::string description,
                                  std::vector<std::string> aliases) {
  commands_.push_back({.id = std::move(id),
                      .name = std::move(name),
                      .description = std::move(description),
                      .aliases = std::move(aliases),
                      .handler = std::nullopt});
}

void SlashCommandRegistry::SetHandler(const std::string& id,
                                      std::function<void()> handler) {
  auto it =
      std::ranges::find_if(commands_, [&](const auto& cmd) { return cmd.id == id; });
  if (it != commands_.end()) {
    it->handler = std::move(handler);
  }
}

void SlashCommandRegistry::Register(SlashCommand command) {
  if (command.id.empty()) {
    command.id = command.name;
  }
  commands_.push_back(std::move(command));
}

bool SlashCommandRegistry::TryDispatch(const std::string& input) const {
  if (input.empty() || input.front() != '/') {
    return false;
  }

  auto name = ExtractCommandName(input);
  if (name.empty()) {
    return false;
  }

  auto command = std::ranges::find_if(commands_, [&](const auto& cmd) {
    if (cmd.name == name) {
      return true;
    }
    return std::ranges::find(cmd.aliases, name) != cmd.aliases.end();
  });
  if (command != commands_.end() && command->handler.has_value()) {
    (*command->handler)();
    return true;
  }

  return false;
}

const std::vector<SlashCommand>& SlashCommandRegistry::Commands() const {
  return commands_;
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
}

}  // namespace yac::presentation
