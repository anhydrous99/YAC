#include "composer_state.hpp"

#include "ftxui/screen/string.hpp"
#include "slash_command_registry.hpp"
#include "util/glyph_util.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <optional>
#include <utility>

namespace yac::presentation {

namespace {

struct ComposerGlyph {
  size_t start = 0;
  size_t end = 0;
  int width = 0;
  bool space = false;
};

int CountNewlines(const std::string& text) {
  return static_cast<int>(std::accumulate(
      text.begin(), text.end(), 0,
      [](int count, char ch) { return count + static_cast<int>(ch == '\n'); }));
}

std::vector<ComposerGlyph> BuildGlyphs(const std::string& text, size_t start,
                                       size_t end) {
  std::vector<ComposerGlyph> glyphs;
  for (size_t pos = start; pos < end;) {
    const size_t next = util::NextGlyphEnd(text, pos, end);
    const std::string glyph = text.substr(pos, next - pos);
    glyphs.push_back(ComposerGlyph{
        .start = pos,
        .end = next,
        .width = std::max(1, ftxui::string_width(glyph)),
        .space = glyph == " ",
    });
    pos = next;
  }
  return glyphs;
}

void PushVisualLine(std::vector<ComposerVisualLine>& lines,
                    const std::string& text, size_t start, size_t end) {
  lines.push_back(ComposerVisualLine{
      .text = text.substr(start, end - start),
      .start = start,
      .end = end,
  });
}

void WrapHardLine(std::vector<ComposerVisualLine>& lines,
                  const std::string& text, size_t start, size_t end,
                  int wrap_width) {
  if (start == end) {
    PushVisualLine(lines, text, start, end);
    return;
  }

  const auto glyphs = BuildGlyphs(text, start, end);
  size_t row_start = 0;
  size_t i = 0;
  int row_width = 0;
  std::optional<size_t> last_space;

  while (i < glyphs.size()) {
    const int next_width = glyphs[i].width;
    if (row_width > 0 && row_width + next_width > wrap_width) {
      if (glyphs[i].space) {
        PushVisualLine(lines, text, glyphs[row_start].start, glyphs[i].start);
        row_start = i;
        while (row_start < glyphs.size() && glyphs[row_start].space) {
          ++row_start;
        }
        i = row_start;
      } else if (last_space.has_value() && *last_space > row_start) {
        PushVisualLine(lines, text, glyphs[row_start].start,
                       glyphs[*last_space].start);

        row_start = *last_space;
        while (row_start < glyphs.size() && glyphs[row_start].space) {
          ++row_start;
        }
        i = row_start;
      } else {
        PushVisualLine(lines, text, glyphs[row_start].start, glyphs[i].start);
        row_start = i;
      }
      row_width = 0;
      last_space.reset();
      continue;
    }

    row_width += next_width;
    if (glyphs[i].space && i > row_start) {
      last_space = i;
    }
    ++i;
  }

  if (row_start < glyphs.size()) {
    PushVisualLine(lines, text, glyphs[row_start].start, end);
  }
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

int ComposerState::CalculateHeight(int max_lines, int wrap_width) const {
  if (content_.empty()) {
    return 1;
  }
  return std::min(static_cast<int>(VisualLines(wrap_width).size()), max_lines);
}

std::vector<ComposerVisualLine> ComposerState::VisualLines(
    int wrap_width) const {
  const int width = std::max(1, wrap_width);
  std::vector<ComposerVisualLine> lines;
  if (content_.empty()) {
    PushVisualLine(lines, content_, 0, 0);
    return lines;
  }

  size_t line_start = 0;
  while (line_start <= content_.size()) {
    const size_t line_end = content_.find('\n', line_start);
    const size_t hard_end =
        line_end == std::string::npos ? content_.size() : line_end;
    WrapHardLine(lines, content_, line_start, hard_end, width);
    if (line_end == std::string::npos) {
      break;
    }
    line_start = line_end + 1;
  }

  return lines;
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
  auto text = content_.substr(1, pos - 1);
  auto space = text.find(' ');
  if (space != std::string::npos) {
    text.resize(space);
  }
  return text;
}

std::vector<int> ComposerState::FilteredSlashIndices(
    const std::vector<SlashCommand>& commands) const {
  auto filter = SlashMenuFilter();
  std::vector<int> indices;
  for (int i = 0; std::cmp_less(i, commands.size()); ++i) {
    if (filter.empty()) {
      indices.push_back(i);
      continue;
    }
    if (commands[i].name.starts_with(filter)) {
      indices.push_back(i);
      continue;
    }
    bool alias_match = false;
    for (const auto& alias : commands[i].aliases) {
      if (alias.starts_with(filter)) {
        alias_match = true;
        break;
      }
    }
    if (alias_match) {
      indices.push_back(i);
    }
  }
  return indices;
}

}  // namespace yac::presentation
