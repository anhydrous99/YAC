#pragma once

#include <ftxui/screen/color.hpp>

namespace yac::presentation::theme {

struct RoleColors {
  ftxui::Color user;
  ftxui::Color agent;
};

struct MarkdownColors {
  ftxui::Color heading;
  ftxui::Color link;
  ftxui::Color quote_border;
  ftxui::Color quote_bg;
  ftxui::Color separator;
};

struct CodeColors {
  ftxui::Color bg;
  ftxui::Color fg;
  ftxui::Color inline_bg;
  ftxui::Color inline_fg;
  ftxui::Color block_border;
};

struct SyntaxColors {
  ftxui::Color keyword;
  ftxui::Color string;
  ftxui::Color comment;
  ftxui::Color number;
  ftxui::Color type;
  ftxui::Color function;
};

struct ChromeColors {
  ftxui::Color border;
  ftxui::Color dim_text;
  ftxui::Color body_text;
  ftxui::Color prompt;
};

struct CardColors {
  ftxui::Color user_bg;
  ftxui::Color agent_bg;
  ftxui::Color user_border;
  ftxui::Color agent_border;
};

struct Theme {
  RoleColors role;
  MarkdownColors markdown;
  CodeColors code;
  SyntaxColors syntax;
  ChromeColors chrome;
  CardColors cards;

  [[nodiscard]] static const Theme& Instance();
};

[[nodiscard]] Theme CatppuccinMocha();

}  // namespace yac::presentation::theme
