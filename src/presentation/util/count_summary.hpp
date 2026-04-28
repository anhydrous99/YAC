#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace yac::presentation::util {

[[nodiscard]] inline std::string CountSummary(std::size_t count,
                                                std::string_view singular,
                                                std::string_view plural) {
  std::string result = std::to_string(count);
  result.push_back(' ');
  result.append(count == 1 ? singular : plural);
  return result;
}

}  // namespace yac::presentation::util
