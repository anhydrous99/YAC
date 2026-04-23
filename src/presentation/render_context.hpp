#pragma once

#include "theme.hpp"

namespace yac::presentation {

struct RenderContext {
  int terminal_width = 80;
  int thinking_frame = 0;

  [[nodiscard]] const theme::Theme& Colors() const {
    return theme::CurrentTheme();
  }
};

}  // namespace yac::presentation
