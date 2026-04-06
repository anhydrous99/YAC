#include "theme.hpp"

namespace yac::presentation::theme {

Theme CatppuccinMocha() {
  Theme t;
  t.role.user = ftxui::Color::RGB(137, 180, 250);
  t.role.agent = ftxui::Color::RGB(166, 227, 161);

  t.markdown.heading = ftxui::Color::RGB(205, 214, 244);
  t.markdown.link = ftxui::Color::RGB(116, 199, 236);
  t.markdown.quote_border = ftxui::Color::RGB(250, 179, 135);
  t.markdown.quote_bg = ftxui::Color::RGB(30, 30, 46);
  t.markdown.separator = ftxui::Color::RGB(49, 50, 68);

  t.code.bg = ftxui::Color::RGB(30, 30, 46);
  t.code.fg = ftxui::Color::RGB(205, 214, 244);
  t.code.inline_bg = ftxui::Color::RGB(49, 50, 68);
  t.code.inline_fg = ftxui::Color::RGB(235, 160, 172);
  t.code.block_border = ftxui::Color::RGB(69, 71, 90);

  t.syntax.keyword = ftxui::Color::RGB(203, 166, 247);
  t.syntax.string = ftxui::Color::RGB(166, 227, 161);
  t.syntax.comment = ftxui::Color::RGB(108, 112, 134);
  t.syntax.number = ftxui::Color::RGB(250, 179, 135);
  t.syntax.type = ftxui::Color::RGB(249, 226, 175);
  t.syntax.function = ftxui::Color::RGB(137, 180, 250);

  t.chrome.border = ftxui::Color::RGB(88, 91, 112);
  t.chrome.dim_text = ftxui::Color::RGB(108, 112, 134);
  t.chrome.prompt = ftxui::Color::RGB(137, 180, 250);

  t.cards.user_bg = ftxui::Color::RGB(30, 30, 46);
  t.cards.agent_bg = ftxui::Color::RGB(24, 24, 37);
  t.cards.user_border = ftxui::Color::RGB(69, 71, 90);
  t.cards.agent_border = ftxui::Color::RGB(69, 71, 90);

  return t;
}

const Theme& Theme::Instance() {
  static const Theme instance = CatppuccinMocha();
  return instance;
}

}  // namespace yac::presentation::theme
