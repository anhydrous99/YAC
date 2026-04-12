#pragma once

#include "ftxui/component/component.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace yac::presentation {

struct Command {
  Command() = default;
  Command(std::string name, std::string description)
      : id(name), name(std::move(name)), description(std::move(description)) {}
  Command(std::string id, std::string name, std::string description)
      : id(std::move(id)),
        name(std::move(name)),
        description(std::move(description)) {}

  std::string id;
  std::string name;
  std::string description;
};

ftxui::Component CommandPalette(std::vector<Command> commands,
                                std::function<void(int)> on_select, bool* show);

}  // namespace yac::presentation
