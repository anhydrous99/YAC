#pragma once

#include <string>
#include <string_view>

namespace yac::tool_call {

// Compile a glob pattern to a regex string.
// Supports: ** (any depth, including zero), * (any chars within one segment),
// ? (single char within one segment). Path separator is '/'.
// Patterns are anchored start-to-end.
[[nodiscard]] std::string GlobToRegex(std::string_view glob);

// Match a path against a glob pattern.
[[nodiscard]] bool MatchesGlob(std::string_view path, std::string_view glob);

}  // namespace yac::tool_call
