#include "provider/zai_context_windows.hpp"

#include <array>
#include <string_view>
#include <utility>

namespace yac::provider {

int KnownZaiContextWindow(std::string_view model_id) {
  if (model_id.empty()) {
    return 0;
  }
  // Ordered most-specific → least-specific. First matching prefix wins, so
  // `glm-4.5-air` resolves before falling through to `glm-4`.
  static constexpr std::array<std::pair<std::string_view, int>, 7> kTable = {{
      {"glm-5.1", 200000},
      {"glm-5", 200000},
      {"glm-4.7", 200000},
      {"glm-4.6", 200000},
      {"glm-4.5", 128000},
      {"glm-4", 128000},
      {"glm-", 128000},
  }};
  for (const auto& [prefix, tokens] : kTable) {
    if (model_id.size() >= prefix.size() && model_id.starts_with(prefix)) {
      return tokens;
    }
  }
  return 0;
}

}  // namespace yac::provider
