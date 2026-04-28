#pragma once

#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace yac::util {

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

[[nodiscard]] inline constexpr bool IsAsciiSpace(char ch) noexcept {
  const auto u = static_cast<unsigned char>(ch);
  return u == ' ' || u == '\t' || u == '\n' || u == '\r' || u == '\v' ||
         u == '\f';
}

[[nodiscard]] inline std::string_view TrimLeftSv(std::string_view s) noexcept {
  while (!s.empty() && IsAsciiSpace(s.front())) {
    s.remove_prefix(1);
  }
  return s;
}

[[nodiscard]] inline std::string_view TrimRightSv(std::string_view s) noexcept {
  while (!s.empty() && IsAsciiSpace(s.back())) {
    s.remove_suffix(1);
  }
  return s;
}

[[nodiscard]] inline std::string_view TrimSv(std::string_view s) noexcept {
  return TrimRightSv(TrimLeftSv(s));
}

[[nodiscard]] inline std::string Trim(std::string_view s) {
  return std::string(TrimSv(s));
}

[[nodiscard]] inline std::string TrimLeft(std::string_view s) {
  return std::string(TrimLeftSv(s));
}

[[nodiscard]] inline std::string TrimRight(std::string_view s) {
  return std::string(TrimRightSv(s));
}

[[nodiscard]] inline std::string ToLowerAscii(std::string_view s) {
  std::string result;
  result.reserve(s.size());
  for (char c : s) {
    result.push_back(
        static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
  }
  return result;
}

}  // namespace yac::util
