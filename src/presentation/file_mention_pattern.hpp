#pragma once

#include <cstddef>
#include <string_view>
#include <vector>

namespace yac::presentation {

struct MentionSpan {
  std::size_t start = 0;  // offset of '@'
  std::size_t end = 0;    // one past last path char
};

// Finds every `@path` token in `text` where `path` is one or more chars from
// `[A-Za-z0-9_./-]`. Tokens are returned in document order. The function is
// intentionally liberal: it does not gate by surrounding whitespace; use
// IsMentionAtTokenBoundary for the inliner-side gate that distinguishes
// `@foo` from `me@host.com`.
[[nodiscard]] std::vector<MentionSpan> FindMentionSpans(std::string_view text);

// Returns the workspace-relative path component (the bytes between '@' and
// the end of the span). Empty if the span is degenerate.
[[nodiscard]] std::string_view MentionPath(std::string_view text,
                                           MentionSpan span);

// True when the char before the '@' is whitespace or the '@' sits at the
// start of `text`. This is the gate that prevents `me@host.com` from
// inlining or triggering the composer menu.
[[nodiscard]] bool IsMentionAtTokenBoundary(std::string_view text,
                                            MentionSpan span);

}  // namespace yac::presentation
