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

[[nodiscard]] inline int CalculateMaxScrollOffset(int content_height,
                                                  int viewport_height) {
  return std::max(0, content_height - viewport_height);
}

[[nodiscard]] inline int ClampScrollOffset(int scroll_offset_y,
                                           int content_height,
                                           int viewport_height) {
  return std::max(
      0, std::min(scroll_offset_y,
                  CalculateMaxScrollOffset(content_height, viewport_height)));
}

[[nodiscard]] inline int CalculateFrameFocusY(int scroll_offset_y,
                                              int viewport_height) {
  int frame_height = std::max(0, viewport_height - 1);
  return scroll_offset_y + frame_height / 2;
}

[[nodiscard]] inline int CalculateThumbPosition(int scroll_offset_y,
                                                int content_height,
                                                int viewport_height,
                                                int track_height,
                                                int thumb_size) {
  int max_scroll_offset =
      CalculateMaxScrollOffset(content_height, viewport_height);
  if (max_scroll_offset <= 0) {
    return 0;
  }
  int clamped =
      ClampScrollOffset(scroll_offset_y, content_height, viewport_height);
  int pos = (track_height - thumb_size) * clamped / max_scroll_offset;
  return std::max(0, std::min(pos, track_height - thumb_size));
}

[[nodiscard]] inline int CalculateScrollOffsetFromRatio(float ratio,
                                                        int content_height,
                                                        int viewport_height) {
  int max_scroll_offset =
      CalculateMaxScrollOffset(content_height, viewport_height);
  return static_cast<int>(ratio * static_cast<float>(max_scroll_offset));
}

[[nodiscard]] inline float CalculateScrollRatio(int scroll_offset_y,
                                                int content_height,
                                                int viewport_height) {
  int max_scroll_offset =
      CalculateMaxScrollOffset(content_height, viewport_height);
  if (max_scroll_offset <= 0) {
    return 0.0F;
  }
  int clamped =
      ClampScrollOffset(scroll_offset_y, content_height, viewport_height);
  return static_cast<float>(clamped) / static_cast<float>(max_scroll_offset);
}

}  // namespace yac::presentation::util
