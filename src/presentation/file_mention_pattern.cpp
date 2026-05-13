#include "presentation/file_mention_pattern.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace yac::presentation {

namespace {

bool IsPathChar(char c) {
  return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
         (c >= '0' && c <= '9') || c == '_' || c == '.' || c == '/' || c == '-';
}

bool IsBoundaryChar(char c) {
  return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

}  // namespace

std::vector<MentionSpan> FindMentionSpans(std::string_view text) {
  std::vector<MentionSpan> out;
  std::size_t i = 0;
  while (i < text.size()) {
    if (text[i] != '@') {
      ++i;
      continue;
    }
    std::size_t j = i + 1;
    while (j < text.size() && IsPathChar(text[j])) {
      ++j;
    }
    if (j > i + 1) {
      out.push_back(MentionSpan{.start = i, .end = j});
      i = j;
    } else {
      ++i;
    }
  }
  return out;
}

std::string_view MentionPath(std::string_view text, MentionSpan span) {
  if (span.end > text.size() || span.start + 1 >= span.end) {
    return {};
  }
  return text.substr(span.start + 1, span.end - span.start - 1);
}

bool IsMentionAtTokenBoundary(std::string_view text, MentionSpan span) {
  if (span.start == 0) {
    return true;
  }
  if (span.start > text.size()) {
    return false;
  }
  return IsBoundaryChar(text[span.start - 1]);
}

}  // namespace yac::presentation
