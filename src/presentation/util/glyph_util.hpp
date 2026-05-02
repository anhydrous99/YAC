#pragma once

#include <algorithm>
#include <cstddef>
#include <string>

namespace yac::presentation::util {

inline std::size_t NextGlyphEnd(const std::string& text, std::size_t start,
                                std::size_t limit) {
  if (start >= limit) {
    return limit;
  }

  const auto byte = static_cast<unsigned char>(text[start]);
  std::size_t advance = 1;
  if ((byte & 0b1110'0000) == 0b1100'0000) {
    advance = 2;
  } else if ((byte & 0b1111'0000) == 0b1110'0000) {
    advance = 3;
  } else if ((byte & 0b1111'1000) == 0b1111'0000) {
    advance = 4;
  }
  return std::min(start + advance, limit);
}

}  // namespace yac::presentation::util
