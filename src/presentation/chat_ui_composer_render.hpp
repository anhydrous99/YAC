#pragma once

#include <string_view>

#include "composer_state.hpp"
#include "ftxui/dom/elements.hpp"

namespace yac::presentation::detail {

inline constexpr std::string_view kComposerPrompt = " \xe2\x9d\xaf ";

int ComposerInputWrapWidth(int terminal_width, int max_input_lines);

ftxui::Element RenderWrappedComposerInput(ComposerState& composer,
                                          int wrap_width, bool focused);

}  // namespace yac::presentation::detail
