#include "slash_command_menu.hpp"

#include "ftxui/dom/elements.hpp"
#include "slash_command_registry.hpp"
#include "theme.hpp"

namespace yac::presentation {

namespace {

inline const auto& k_theme = theme::Theme::Instance();
constexpr int kMenuMaxWidth = 50;
constexpr int kMenuMaxVisible = 8;

ftxui::Element RenderRow(const SlashCommand& command, bool selected) {
  auto name = ftxui::text("/" + command.name) | ftxui::bold |
              ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 14);
  auto desc = ftxui::text(command.description) | ftxui::flex;

  if (selected) {
    name = name | ftxui::color(k_theme.dialog.selected_fg);
    desc = desc | ftxui::color(k_theme.dialog.selected_fg);
    return ftxui::hbox({name, desc}) |
           ftxui::bgcolor(k_theme.dialog.selected_bg);
  }
  name = name | ftxui::color(k_theme.dialog.input_fg);
  desc = desc | ftxui::color(k_theme.dialog.dim_text) | ftxui::dim;
  return ftxui::hbox({name, desc});
}

}  // namespace

ftxui::Element RenderSlashCommandMenu(const std::vector<SlashCommand>& commands,
                                      const std::vector<int>& filtered_indices,
                                      int selected_index, int max_width) {
  if (filtered_indices.empty()) {
    return ftxui::text("  No matching commands") |
           ftxui::color(k_theme.dialog.dim_text) | ftxui::dim |
           ftxui::bgcolor(k_theme.dialog.input_bg) |
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
  return content | ftxui::bgcolor(k_theme.dialog.input_bg) |
         ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, width);
}

}  // namespace yac::presentation
