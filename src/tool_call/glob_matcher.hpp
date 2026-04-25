#pragma once

#include <regex>
#include <string>
#include <string_view>

namespace yac::tool_call {

// Compile a glob pattern to a regex string.
// Supports: ** (any depth, including zero), * (any chars within one segment),
// ? (single char within one segment). Path separator is '/'.
// Patterns are anchored start-to-end.
[[nodiscard]] std::string GlobToRegex(std::string_view glob);

// Pre-compiled glob pattern for efficient repeated matching.
// Compile once, then call Match() many times.
class CompiledGlob {
 public:
  explicit CompiledGlob(std::string_view glob);

  [[nodiscard]] bool Match(std::string_view path) const;

 private:
  std::regex re_;
};

// Match a path against a glob pattern.
// Convenience wrapper — compiles the regex on every call.
// Prefer CompiledGlob when matching many paths against the same pattern.
[[nodiscard]] bool MatchesGlob(std::string_view path, std::string_view glob);

}  // namespace yac::tool_call
