#include "chat_ui_composer_render.hpp"

#include "file_mention_pattern.hpp"
#include "ftxui/screen/string.hpp"
#include "theme.hpp"
#include "ui_spacing.hpp"
#include "util/glyph_util.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>
#include <vector>

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

struct LineSegment {
  std::string text;
  bool is_mention = false;
};

std::vector<LineSegment> SegmentLine(
    const std::string& line_text, std::size_t line_start,
    const std::vector<MentionSpan>& mention_spans) {
  std::vector<LineSegment> segments;
  const std::size_t line_end = line_start + line_text.size();
  std::size_t local = 0;
  for (const auto& span : mention_spans) {
    if (span.end <= line_start) {
      continue;
    }
    if (span.start >= line_end) {
      break;
    }
    const std::size_t s = std::max(span.start, line_start) - line_start;
    const std::size_t e = std::min(span.end, line_end) - line_start;
    if (local < s) {
      segments.push_back(LineSegment{.text = line_text.substr(local, s - local),
                                     .is_mention = false});
    }
    segments.push_back(
        LineSegment{.text = line_text.substr(s, e - s), .is_mention = true});
    local = e;
  }
  if (local < line_text.size()) {
    segments.push_back(
        LineSegment{.text = line_text.substr(local), .is_mention = false});
  }
  return segments;
}

ftxui::Element StyleSegment(const std::string& text, bool is_mention) {
  auto el = ftxui::text(text);
  if (is_mention) {
    el |= ftxui::color(theme::CurrentTheme().semantic.accent_secondary) |
          ftxui::underlined;
  }
  return el;
}

ftxui::Element RenderComposerVisualLine(
    const ComposerVisualLine& line, std::size_t cursor, bool show_cursor,
    bool focused, const std::vector<MentionSpan>& mention_spans) {
  const auto segments = SegmentLine(line.text, line.start, mention_spans);

  if (!show_cursor) {
    if (segments.empty()) {
      return ftxui::text("") | ftxui::xflex;
    }
    ftxui::Elements elements;
    elements.reserve(segments.size());
    for (const auto& seg : segments) {
      elements.push_back(StyleSegment(seg.text, seg.is_mention));
    }
    return ftxui::hbox(std::move(elements)) | ftxui::xflex;
  }

  const std::size_t clamped_cursor = std::clamp(cursor, line.start, line.end);
  const std::size_t local_cursor = clamped_cursor - line.start;

  ftxui::Elements elements;
  std::size_t pos = 0;
  bool cursor_emitted = false;
  for (const auto& seg : segments) {
    const std::size_t seg_end = pos + seg.text.size();
    if (!cursor_emitted && local_cursor >= pos && local_cursor < seg_end) {
      const std::size_t in_pos = local_cursor - pos;
      const std::size_t glyph_end =
          util::NextGlyphEnd(seg.text, in_pos, seg.text.size());
      if (in_pos > 0) {
        elements.push_back(
            StyleSegment(seg.text.substr(0, in_pos), seg.is_mention));
      }
      elements.push_back(
          CursorCell(seg.text.substr(in_pos, glyph_end - in_pos), focused));
      if (glyph_end < seg.text.size()) {
        elements.push_back(
            StyleSegment(seg.text.substr(glyph_end), seg.is_mention));
      }
      cursor_emitted = true;
    } else {
      elements.push_back(StyleSegment(seg.text, seg.is_mention));
    }
    pos = seg_end;
  }

  if (!cursor_emitted) {
    elements.push_back(CursorCell(" ", focused));
  }

  return ftxui::hbox(std::move(elements)) | ftxui::xflex;
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

  const auto& content = composer.Content();
  const auto raw_spans = FindMentionSpans(content);
  std::vector<MentionSpan> mention_spans;
  mention_spans.reserve(raw_spans.size());
  for (const auto& span : raw_spans) {
    if (IsMentionAtTokenBoundary(content, span)) {
      mention_spans.push_back(span);
    }
  }

  ftxui::Elements rows;
  rows.reserve(lines.size());
  for (std::size_t i = 0; i < lines.size(); ++i) {
    rows.push_back(RenderComposerVisualLine(lines[i], cursor, i == cursor_line,
                                            focused, mention_spans));
  }
  return ftxui::vbox(std::move(rows)) | ftxui::frame;
}

}  // namespace yac::presentation::detail
