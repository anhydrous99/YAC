#pragma once

#include <algorithm>

namespace yac::presentation::util {

[[nodiscard]] inline bool ShouldShowScrollbar(int content_height,
                                              int viewport_height) {
  return content_height > viewport_height;
}

[[nodiscard]] inline int CalculateThumbSize(int content_height,
                                            int viewport_height,
                                            int track_height) {
  if (content_height <= 0) {
    return 1;
  }
  return std::max(1, track_height * viewport_height / content_height);
}

[[nodiscard]] inline int CalculateThumbPosition(int scroll_focus_y,
                                                int content_height,
                                                int track_height,
                                                int thumb_size) {
  if (content_height <= 0) {
    return 0;
  }
  int pos = (track_height - thumb_size) * scroll_focus_y / content_height;
  return std::max(0, std::min(pos, track_height - thumb_size));
}

[[nodiscard]] inline int CalculateScrollFocusFromRatio(float ratio,
                                                       int content_height) {
  return static_cast<int>(ratio * static_cast<float>(content_height));
}

[[nodiscard]] inline float CalculateScrollRatio(int scroll_focus_y,
                                                int content_height) {
  if (scroll_focus_y >= 10000) {
    return 1.0F;
  }
  if (content_height <= 0) {
    return 0.0F;
  }
  return static_cast<float>(scroll_focus_y) /
         static_cast<float>(content_height);
}

}  // namespace yac::presentation::util
