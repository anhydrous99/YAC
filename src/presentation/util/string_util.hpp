#pragma once

#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

namespace yac::presentation::util {

[[nodiscard]] inline std::vector<std::string> SplitLines(
    std::string_view text) {
  std::vector<std::string> lines;
  std::string line;
  for (char c : text) {
    if (c == '\n') {
      lines.push_back(std::move(line));
      line.clear();
    } else {
      line += c;
    }
  }
  if (!line.empty() || text.empty()) {
    lines.push_back(std::move(line));
  }
  return lines;
}

[[nodiscard]] inline std::string Trim(std::string_view s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return "";
  }
  auto end = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(start, end - start + 1));
}

[[nodiscard]] inline std::string TrimLeft(std::string_view s) {
  auto start = s.find_first_not_of(" \t");
  if (start == std::string_view::npos) {
    return "";
  }
  return std::string(s.substr(start));
}

}  // namespace yac::presentation::util
