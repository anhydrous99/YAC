#include "slash_command_menu.hpp"

#include "ftxui/dom/elements.hpp"
#include "slash_command_registry.hpp"
#include "theme.hpp"

namespace yac::presentation {

namespace {
constexpr int kMenuMaxWidth = 50;
constexpr int kMenuMaxVisible = 8;

ftxui::Element RenderRow(const SlashCommand& command, bool selected) {
  std::string label = "/" + command.name;
  for (const auto& alias : command.aliases) {
    label += ", /" + alias;
  }
  auto indicator = ftxui::text(selected ? " \xe2\x96\xb8 " : "   ");
  auto name = ftxui::text(label) | ftxui::bold |
              ftxui::size(ftxui::WIDTH, ftxui::GREATER_THAN, 14);
  auto desc = ftxui::text(command.description) | ftxui::flex;

  if (selected) {
    indicator = indicator | ftxui::color(theme::CurrentTheme().dialog.selected_fg);
    name = name | ftxui::color(theme::CurrentTheme().dialog.selected_fg);
    desc = desc | ftxui::color(theme::CurrentTheme().dialog.selected_fg);
    return ftxui::hbox({indicator, name, desc}) |
           ftxui::bgcolor(theme::CurrentTheme().dialog.selected_bg);
  }
  indicator = indicator | ftxui::color(theme::CurrentTheme().dialog.dim_text);
  name = name | ftxui::color(theme::CurrentTheme().dialog.input_fg);
  desc = desc | ftxui::color(theme::CurrentTheme().dialog.dim_text) |
         ftxui::dim;
  return ftxui::hbox({indicator, name, desc});
}

}  // namespace

ftxui::Element RenderSlashCommandMenu(const std::vector<SlashCommand>& commands,
                                      const std::vector<int>& filtered_indices,
                                      int selected_index, int max_width) {
  if (filtered_indices.empty()) {
    return ftxui::text("  No matching commands") |
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
  return content | ftxui::border |
         ftxui::bgcolor(theme::CurrentTheme().dialog.input_bg) |
         ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, width);
}

}  // namespace yac::presentation
