#pragma once

#include <chrono>
#include <string>

namespace yac::presentation::util {

[[nodiscard]] inline std::string FormatRelativeTime(
    std::chrono::system_clock::time_point tp) {
  auto now = std::chrono::system_clock::now();
  auto diff =
      std::chrono::duration_cast<std::chrono::seconds>(now - tp).count();
  if (diff < 0) {
    return "just now";
  }
  if (diff < 60) {
    return "just now";
  }
  if (diff < 3600) {
    return std::to_string(diff / 60) + "m ago";
  }
  if (diff < 86400) {
    return std::to_string(diff / 3600) + "h ago";
  }
  return std::to_string(diff / 86400) + "d ago";
}

}  // namespace yac::presentation::util
