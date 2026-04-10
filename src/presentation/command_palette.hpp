#pragma once

#include "ftxui/component/component.hpp"

#include <functional>
#include <string>
#include <vector>

namespace yac::presentation {

struct Command {
  std::string name;
  std::string description;
};

ftxui::Component CommandPalette(std::vector<Command> commands,
                                std::function<void(int)> on_select, bool* show);

}  // namespace yac::presentation
