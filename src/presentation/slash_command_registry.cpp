#include "slash_command_registry.hpp"

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
  for (std::size_t i = 0; i < commands_.size();
       ++i) {  // NOLINT(modernize-loop-convert)
    if (commands_[i].id == id) {
      commands_[i].handler = std::move(handler);
      break;
    }
  }
}

void SlashCommandRegistry::SetArgumentsHandler(
    const std::string& id, std::function<void(std::string)> handler) {
  for (std::size_t i = 0; i < commands_.size();
       ++i) {  // NOLINT(modernize-loop-convert)
    if (commands_[i].id == id) {
      commands_[i].arguments_handler = std::move(handler);
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

  for (std::size_t i = 0; i < commands_.size();
       ++i) {  // NOLINT(modernize-loop-convert)
    const auto& command = commands_[i];
    auto alias_match = false;
    for (std::size_t j = 0; j < command.aliases.size();
         ++j) {  // NOLINT(modernize-loop-convert)
      if (command.aliases[j] == name) {
        alias_match = true;
        break;
      }
    }

    if (command.name == name || alias_match) {
      if (command.arguments_handler.has_value()) {
        auto args_start = name.length() + 1;
        while (args_start < input.size() &&
               (input[args_start] == ' ' || input[args_start] == '\t' ||
                input[args_start] == '\n' || input[args_start] == '\r' ||
                input[args_start] == '\f' || input[args_start] == '\v')) {
          ++args_start;
        }
        auto args = input.substr(args_start);
        (*command.arguments_handler)(std::move(args));
        return true;
      }
      const auto& handler = command.handler;
      if (handler.has_value()) {
        (*handler)();
        return true;
      }
    }
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
  registry.Define("clear", "clear", "Clear the conversation");
  registry.Define("cancel", "cancel", "Cancel the active response");
  registry.Define("help", "help", "Show shortcuts and setup status", {"?"});
}

}  // namespace yac::presentation
