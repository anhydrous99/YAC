#include "file_mention_menu.hpp"

#include "ftxui/dom/elements.hpp"
#include "theme.hpp"
#include "ui_spacing.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace yac::presentation {

namespace {

constexpr int kMenuMaxWidth = 60;
constexpr int kMenuMaxVisible = 8;

std::string FormatSize(std::uintmax_t bytes) {
  constexpr std::uintmax_t kKb = 1024UL;
  constexpr std::uintmax_t kMb = 1024UL * 1024UL;
  if (bytes < kKb) {
    return std::to_string(bytes) + "B";
  }
  if (bytes < kMb) {
    return std::to_string(bytes / kKb) + "K";
  }
  return std::to_string(bytes / kMb) + "M";
}

ftxui::Element RenderRow(const tool_call::FileMentionRow& row, bool selected) {
  auto name = ftxui::text("@" + row.relative_path) | ftxui::bold | ftxui::flex;
  auto size = ftxui::text(" " + FormatSize(row.size_bytes));

  if (selected) {
    auto bar = ftxui::text(" ") |
               ftxui::bgcolor(theme::CurrentTheme().semantic.accent_primary);
    name |= ftxui::color(theme::CurrentTheme().dialog.selected_fg);
    size |= ftxui::color(theme::CurrentTheme().semantic.text_weak);
    return ftxui::hbox({bar, ftxui::text(std::string(layout::kRowGap, ' ')),
                        name, size}) |
           ftxui::bgcolor(theme::CurrentTheme().semantic.selection_bg);
  }
  name |= ftxui::color(theme::CurrentTheme().dialog.input_fg);
  size |= ftxui::color(theme::CurrentTheme().semantic.text_weak) | ftxui::dim;
  return ftxui::hbox(
      {ftxui::text(std::string(layout::kCardPadX, ' ')), name, size});
}

}  // namespace

ftxui::Element RenderFileMentionMenu(
    const std::vector<tool_call::FileMentionRow>& rows, int selected_index,
    int max_width, bool indexing) {
  if (rows.empty()) {
    const std::string label =
        indexing ? "Indexing workspace…" : "No matching files";
    return ftxui::text(std::string(layout::kCardPadX, ' ') + label) |
           ftxui::color(theme::CurrentTheme().dialog.dim_text) | ftxui::dim |
           ftxui::bgcolor(theme::CurrentTheme().dialog.input_bg) |
           ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, max_width);
  }

  const int visible_count =
      std::min(static_cast<int>(rows.size()), kMenuMaxVisible);
  ftxui::Elements row_els;
  row_els.reserve(visible_count);
  for (int i = 0; i < visible_count; ++i) {
    row_els.push_back(RenderRow(rows[i], i == selected_index));
  }

  auto content = ftxui::vbox(std::move(row_els));
  const int width = std::min(max_width, kMenuMaxWidth);
  auto top_line = ftxui::text(std::string(width, ' ')) |
                  ftxui::bgcolor(theme::CurrentTheme().semantic.border_subtle) |
                  ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1);
  return ftxui::vbox({top_line, content}) |
         ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, width);
}

}  // namespace yac::presentation
