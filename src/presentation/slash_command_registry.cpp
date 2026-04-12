#include "slash_command_registry.hpp"

#include <utility>

namespace yac::presentation {

void SlashCommandRegistry::Register(SlashCommand command) {
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

  for (const auto& cmd : commands_) {
    if (cmd.name == name) {
      cmd.handler();
      return true;
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

}  // namespace yac::presentation
