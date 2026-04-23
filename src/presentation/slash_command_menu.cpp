#include "slash_command_menu.hpp"

#include "ftxui/dom/elements.hpp"
#include "slash_command_registry.hpp"
#include "theme.hpp"
#include "ui_spacing.hpp"

#include <string>

namespace yac::presentation {

namespace {
constexpr int kMenuMaxWidth = 50;
constexpr int kMenuMaxVisible = 8;

ftxui::Element RenderRow(const SlashCommand& command, bool selected) {
  std::string label = "/" + command.name;
  for (const auto& alias : command.aliases) {
    label += ", /" + alias;
  }
  auto name = ftxui::text(label) | ftxui::bold |
              ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 14);
  auto desc = ftxui::text(command.description) | ftxui::flex;

  if (selected) {
    auto bar = ftxui::text(" ") |
               ftxui::bgcolor(theme::CurrentTheme().semantic.accent_primary);
    name |= ftxui::color(theme::CurrentTheme().dialog.selected_fg);
    desc |= ftxui::color(theme::CurrentTheme().semantic.text_weak);
    return ftxui::hbox({bar, ftxui::text(std::string(layout::kRowGap, ' ')),
                        name, desc}) |
           ftxui::bgcolor(theme::CurrentTheme().semantic.selection_bg);
  }
  name |= ftxui::color(theme::CurrentTheme().dialog.input_fg);
  desc |= ftxui::color(theme::CurrentTheme().semantic.text_weak) | ftxui::dim;
  return ftxui::hbox(
      {ftxui::text(std::string(layout::kCardPadX, ' ')), name, desc});
}

}  // namespace

ftxui::Element RenderSlashCommandMenu(const std::vector<SlashCommand>& commands,
                                      const std::vector<int>& filtered_indices,
                                      int selected_index, int max_width) {
  if (filtered_indices.empty()) {
    return ftxui::text(std::string(layout::kCardPadX, ' ') +
                       "No matching commands") |
           ftxui::color(theme::CurrentTheme().dialog.dim_text) | ftxui::dim |
           ftxui::bgcolor(theme::CurrentTheme().dialog.input_bg) |
           ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, max_width);
  }

  int visible_count =
      std::min(static_cast<int>(filtered_indices.size()), kMenuMaxVisible);
  ftxui::Elements rows;
  for (int i = 0; i < visible_count; ++i) {
    rows.push_back(
        RenderRow(commands[filtered_indices[i]], i == selected_index));
  }

  auto content = ftxui::vbox(std::move(rows));
  int width = std::min(max_width, kMenuMaxWidth);
  auto top_line = ftxui::text(std::string(width, ' ')) |
                  ftxui::bgcolor(theme::CurrentTheme().semantic.border_subtle) |
                  ftxui::size(ftxui::HEIGHT, ftxui::EQUAL, 1);
  return ftxui::vbox({top_line, content}) |
         ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, width);
}

}  // namespace yac::presentation
