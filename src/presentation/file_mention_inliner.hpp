#pragma once

#include "tool_call/workspace_filesystem.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace yac::presentation {

struct InlineDiagnostic {
  std::string path;
  std::string message;
};

struct InlineResult {
  std::string text;
  std::vector<InlineDiagnostic> diagnostics;
};

// Rewrites `user_text` by appending an "Attached files" block containing the
// contents of every @-mentioned workspace file. Only tokens whose '@' is at
// the start of the text or follows whitespace are inlined (so email-style
// `me@host.com` is left alone). Errors per file (missing, outside workspace,
// symlink, binary, etc.) become inline `[error: ...]` markers and are
// also collected in `result.diagnostics`. Per-file content is truncated to
// `per_file_cap`; total attached bytes are capped at `total_cap`. If no
// usable mentions are found, returns `user_text` unchanged.
[[nodiscard]] InlineResult InlineFileMentions(
    std::string_view user_text, const tool_call::WorkspaceFilesystem& fs,
    std::size_t per_file_cap = 64UL * 1024UL,
    std::size_t total_cap = 256UL * 1024UL);

}  // namespace yac::presentation
