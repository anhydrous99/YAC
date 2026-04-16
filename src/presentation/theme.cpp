#include "theme.hpp"

namespace yac::presentation::theme {

Theme CatppuccinMocha() {
  Theme t;
  t.role.user = ftxui::Color::RGB(137, 180, 250);
  t.role.agent = ftxui::Color::RGB(166, 227, 161);
  t.role.error = ftxui::Color::RGB(243, 139, 168);

  t.markdown.heading = ftxui::Color::RGB(205, 214, 244);
  t.markdown.link = ftxui::Color::RGB(116, 199, 236);
  t.markdown.quote_bg = ftxui::Color::RGB(30, 30, 46);

  t.code.bg = ftxui::Color::RGB(30, 30, 46);
  t.code.alt_bg = ftxui::Color::RGB(34, 34, 50);
  t.code.fg = ftxui::Color::RGB(205, 214, 244);
  t.code.inline_bg = ftxui::Color::RGB(49, 50, 68);
  t.code.inline_fg = ftxui::Color::RGB(235, 160, 172);
  t.code.border = ftxui::Color::RGB(69, 71, 90);

  t.syntax.keyword = ftxui::Color::RGB(203, 166, 247);
  t.syntax.string = ftxui::Color::RGB(166, 227, 161);
  t.syntax.comment = ftxui::Color::RGB(166, 173, 200);
  t.syntax.number = ftxui::Color::RGB(250, 179, 135);
  t.syntax.type = ftxui::Color::RGB(249, 226, 175);
  t.syntax.function = ftxui::Color::RGB(137, 180, 250);

  t.chrome.dim_text = ftxui::Color::RGB(147, 153, 178);
  t.chrome.body_text = ftxui::Color::RGB(186, 194, 222);
  t.chrome.prompt = ftxui::Color::RGB(137, 180, 250);

  t.cards.user_bg = ftxui::Color::RGB(30, 30, 46);
  t.cards.agent_bg = ftxui::Color::RGB(24, 24, 37);

  t.tool.header_bg = ftxui::Color::RGB(49, 50, 68);
  t.tool.bash_accent = ftxui::Color::RGB(250, 179, 135);
  t.tool.edit_add = ftxui::Color::RGB(166, 227, 161);
  t.tool.edit_remove = ftxui::Color::RGB(243, 139, 168);
  t.tool.edit_context = ftxui::Color::RGB(205, 214, 244);
  t.tool.read_accent = ftxui::Color::RGB(137, 180, 250);
  t.tool.grep_accent = ftxui::Color::RGB(203, 166, 247);
  t.tool.glob_accent = ftxui::Color::RGB(148, 226, 213);
  t.tool.web_accent = ftxui::Color::RGB(137, 180, 250);
  t.tool.icon_fg = ftxui::Color::RGB(205, 214, 244);

  t.dialog.overlay_bg = ftxui::Color::RGB(17, 17, 27);
  t.dialog.selected_bg = ftxui::Color::RGB(49, 50, 68);
  t.dialog.selected_fg = ftxui::Color::RGB(205, 214, 244);
  t.dialog.input_bg = ftxui::Color::RGB(30, 30, 46);
  t.dialog.input_fg = ftxui::Color::RGB(205, 214, 244);
  t.dialog.dim_text = ftxui::Color::RGB(147, 153, 178);

  return t;
}

const Theme& Theme::Instance() {
  static const Theme instance = CatppuccinMocha();
  return instance;
}

}  // namespace yac::presentation::theme
