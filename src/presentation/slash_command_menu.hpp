#pragma once

#include "ftxui/dom/elements.hpp"

#include <vector>

namespace yac::presentation {

struct SlashCommand;

ftxui::Element RenderSlashCommandMenu(const std::vector<SlashCommand>& commands,
                                      const std::vector<int>& filtered_indices,
                                      int selected_index, int max_width);

}  // namespace yac::presentation
