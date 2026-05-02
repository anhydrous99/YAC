#include "chat_ui_composer_render.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include "ftxui/screen/string.hpp"
#include "ui_spacing.hpp"
#include "util/glyph_util.hpp"

namespace yac::presentation::detail {

namespace {

std::size_t CursorVisualLineIndex(const std::vector<ComposerVisualLine>& lines,
                                  std::size_t cursor) {
  if (lines.empty()) {
    return 0;
  }
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const std::size_t next_start =
        i + 1 < lines.size() ? lines[i + 1].start : lines[i].end;
    if (cursor >= lines[i].start &&
        (cursor <= lines[i].end || cursor < next_start)) {
      return i;
    }
  }
  return lines.size() - 1;
}

ftxui::Element CursorCell(std::string text, bool focused) {
  auto cell = ftxui::text(std::move(text));
  if (focused) {
    cell |= ftxui::focusCursorBarBlinking;
  } else {
    cell |= ftxui::focus;
  }
  return cell;
}

ftxui::Element RenderComposerVisualLine(const ComposerVisualLine& line,
                                        std::size_t cursor, bool show_cursor,
                                        bool focused) {
  if (!show_cursor) {
    return ftxui::text(line.text) | ftxui::xflex;
  }

  const std::size_t clamped_cursor = std::clamp(cursor, line.start, line.end);
  const std::size_t local_cursor = clamped_cursor - line.start;
  if (local_cursor >= line.text.size()) {
    return ftxui::hbox({
               ftxui::text(line.text),
               CursorCell(" ", focused),
           }) |
           ftxui::xflex;
  }

  const std::size_t glyph_end =
      util::NextGlyphEnd(line.text, local_cursor, line.text.size());
  return ftxui::hbox({
             ftxui::text(line.text.substr(0, local_cursor)),
             CursorCell(
                 line.text.substr(local_cursor, glyph_end - local_cursor),
                 focused),
             ftxui::text(line.text.substr(glyph_end)),
         }) |
         ftxui::xflex;
}

}  // namespace

int ComposerInputWrapWidth(int terminal_width, int max_input_lines) {
  const std::string counter = " " + std::to_string(max_input_lines) + "/" +
                              std::to_string(max_input_lines) + " ";
  const int reserved_width = (2 * layout::kComposerPadX) +
                             ftxui::string_width(std::string(kComposerPrompt)) +
                             ftxui::string_width(counter);
  return std::max(1, terminal_width - reserved_width);
}

ftxui::Element RenderWrappedComposerInput(ComposerState& composer,
                                          int wrap_width, bool focused) {
  auto lines = composer.VisualLines(wrap_width);
  const std::size_t cursor =
      static_cast<std::size_t>(std::max(0, *composer.CursorPosition()));
  const std::size_t cursor_line = CursorVisualLineIndex(lines, cursor);

  ftxui::Elements rows;
  rows.reserve(lines.size());
  for (std::size_t i = 0; i < lines.size(); ++i) {
    rows.push_back(
        RenderComposerVisualLine(lines[i], cursor, i == cursor_line, focused));
  }
  return ftxui::vbox(std::move(rows)) | ftxui::frame;
}

}  // namespace yac::presentation::detail
