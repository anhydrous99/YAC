#include "composer_state.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

namespace yac::presentation {

namespace {

int CountNewlines(const std::string& text) {
  return static_cast<int>(std::accumulate(
      text.begin(), text.end(), 0,
      [](int count, char ch) { return count + static_cast<int>(ch == '\n'); }));
}

}  // namespace

bool ComposerState::Empty() const {
  return content_.empty();
}

int ComposerState::CalculateHeight(int max_lines) const {
  if (content_.empty()) {
    return 1;
  }
  int lines = CountNewlines(content_) + 1;
  return std::min(lines, max_lines);
}

std::string& ComposerState::Content() {
  return content_;
}

const std::string& ComposerState::Content() const {
  return content_;
}

int* ComposerState::CursorPosition() {
  return &cursor_;
}

void ComposerState::InsertNewline() {
  content_.insert(static_cast<size_t>(cursor_), "\n");
  ++cursor_;
}

std::string ComposerState::Submit() {
  std::string submitted = std::move(content_);
  content_.clear();
  cursor_ = 0;
  return submitted;
}

}  // namespace yac::presentation
