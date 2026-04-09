#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <utility>

namespace yac::presentation::util {

using RelativeTimeCache = std::optional<
    std::pair<std::string, std::chrono::system_clock::time_point>>;

[[nodiscard]] inline std::string FormatRelativeTime(
    std::chrono::system_clock::time_point tp, RelativeTimeCache& cache) {
  auto now = std::chrono::system_clock::now();
  if (cache.has_value() && now < cache->second) {
    return cache->first;
  }
  auto diff =
      std::chrono::duration_cast<std::chrono::seconds>(now - tp).count();
  std::string result;
  long long boundary_secs = 0;
  if (diff < 60) {
    result = "just now";
    boundary_secs = 60;
  } else if (diff < 3600) {
    result = std::to_string(diff / 60) + "m ago";
    boundary_secs = ((diff / 60) + 1) * 60;
  } else if (diff < 86400) {
    result = std::to_string(diff / 3600) + "h ago";
    boundary_secs = ((diff / 3600) + 1) * 3600;
  } else {
    result = std::to_string(diff / 86400) + "d ago";
    boundary_secs = ((diff / 86400) + 1) * 86400;
  }
  cache = std::make_pair(result, tp + std::chrono::seconds(boundary_secs));
  return result;
}

[[nodiscard]] inline std::string FormatRelativeTime(
    std::chrono::system_clock::time_point tp) {
  RelativeTimeCache unused;
  return FormatRelativeTime(tp, unused);
}

}  // namespace yac::presentation::util
