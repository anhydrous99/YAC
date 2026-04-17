#pragma once

#include <string_view>

namespace yac::app {

// Best-effort lookup of a model's context-window size in tokens. Returns 0 for
// unknown model identifiers; callers should render an "unknown" state in that
// case rather than assuming a default. Matches by literal id first, then by
// common prefix families.
[[nodiscard]] int LookupContextWindow(std::string_view model_id);

}  // namespace yac::app
