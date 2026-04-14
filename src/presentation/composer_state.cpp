#include "composer_state.hpp"

#include "slash_command_registry.hpp"

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
  slash_menu_active_ = false;
  slash_menu_selected_ = 0;
  return submitted;
}

bool ComposerState::IsSlashMenuActive() const {
  return slash_menu_active_;
}

void ComposerState::ActivateSlashMenu() {
  slash_menu_active_ = true;
  slash_menu_selected_ = 0;
}

void ComposerState::DismissSlashMenu() {
  slash_menu_active_ = false;
}

int ComposerState::SlashMenuSelectedIndex() const {
  return slash_menu_selected_;
}

void ComposerState::SetSlashMenuSelectedIndex(int index) {
  slash_menu_selected_ = index;
}

std::string ComposerState::SlashMenuFilter() const {
  if (content_.empty() || content_.front() != '/') {
    return {};
  }
  auto pos = static_cast<size_t>(cursor_);
  if (pos < 1) {
    return {};
  }
  return content_.substr(1, pos - 1);
}

std::vector<int> ComposerState::FilteredSlashIndices(
    const std::vector<SlashCommand>& commands) const {
  auto filter = SlashMenuFilter();
  std::vector<int> indices;
  for (int i = 0; i < static_cast<int>(commands.size()); ++i) {
    if (filter.empty() || commands[i].name.substr(0, filter.size()) == filter) {
      indices.push_back(i);
    }
  }
  return indices;
}

}  // namespace yac::presentation
