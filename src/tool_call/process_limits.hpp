#pragma once

#include <cstddef>

namespace yac::tool_call {

inline constexpr size_t kMaxToolOutputBytes = 16384;
inline constexpr int kSubprocessKillGraceMs = 2000;

}  // namespace yac::tool_call
