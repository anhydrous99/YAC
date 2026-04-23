#pragma once

#include "theme.hpp"

namespace yac::presentation {

struct RenderContext {
  const theme::Theme* theme = &theme::CurrentTheme();
  int terminal_width = 80;
  int thinking_frame = 0;

  [[nodiscard]] const theme::Theme& Colors() const { return *theme; }
};

}  // namespace yac::presentation
