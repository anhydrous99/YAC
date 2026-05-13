#pragma once

#include "core_types/file_mention.hpp"

#include <cstddef>
#include <filesystem>
#include <stop_token>
#include <vector>

namespace yac::tool_call {

struct WalkResult {
  std::vector<FileMentionRow> rows;
  bool used_rg = false;
  bool truncated = false;
  bool cancelled = false;
};

// Lists workspace-relative file paths up to `max_entries`. Prefers
// `rg --files --hidden --no-follow --null` (honors .gitignore + .ignore
// natively) and falls back to recursive_directory_iterator + GitignoreFilter
// if rg is absent or fails. Rows are sorted by (path-length, lexicographic)
// to match the FileIndex menu contract. Cancellation is best-effort via
// `stop_token` (kills the rg subprocess and stops the fallback walker).
[[nodiscard]] WalkResult WalkWorkspace(const std::filesystem::path& root,
                                       std::size_t max_entries,
                                       std::stop_token stop_token);

// Test-only: when true, WalkWorkspace skips the rg attempt and goes straight
// to the in-process walker. Useful for exercising the fallback path on hosts
// where rg is installed.
void SetRipgrepDisabledForTesting(bool disabled);

}  // namespace yac::tool_call
