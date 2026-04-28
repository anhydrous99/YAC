#pragma once

#include <exception>
#include <utility>

namespace yac::tool_call {

template <typename Result, typename Fn>
[[nodiscard]] inline Result CallWithLspErrorTo(Result error_fallback, Fn&& fn) {
  try {
    return std::forward<Fn>(fn)();
  } catch (const std::exception& error) {
    error_fallback.is_error = true;
    error_fallback.error = error.what();
    return error_fallback;
  }
}

}  // namespace yac::tool_call
