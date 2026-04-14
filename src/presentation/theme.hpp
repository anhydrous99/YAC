#pragma once

#include <ftxui/screen/color.hpp>

namespace yac::presentation::theme {

struct RoleColors {
  ftxui::Color user;
  ftxui::Color agent;
  ftxui::Color error;
};

struct MarkdownColors {
  ftxui::Color heading;
  ftxui::Color link;
  ftxui::Color quote_bg;
};

struct CodeColors {
  ftxui::Color bg;
  ftxui::Color alt_bg;
  ftxui::Color fg;
  ftxui::Color inline_bg;
  ftxui::Color inline_fg;
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
  ftxui::Color dim_text;
  ftxui::Color body_text;
  ftxui::Color prompt;
};

struct CardColors {
  ftxui::Color user_bg;
  ftxui::Color agent_bg;
};

struct ToolColors {
  ftxui::Color header_bg;
  ftxui::Color bash_accent;
  ftxui::Color edit_add;
  ftxui::Color edit_remove;
  ftxui::Color edit_context;
  ftxui::Color read_accent;
  ftxui::Color grep_accent;
  ftxui::Color glob_accent;
  ftxui::Color web_accent;
  ftxui::Color icon_fg;
};

struct DialogColors {
  ftxui::Color overlay_bg;
  ftxui::Color selected_bg;
  ftxui::Color selected_fg;
  ftxui::Color input_bg;
  ftxui::Color input_fg;
  ftxui::Color dim_text;
};

struct Theme {
  RoleColors role;
  MarkdownColors markdown;
  CodeColors code;
  SyntaxColors syntax;
  ChromeColors chrome;
  CardColors cards;
  ToolColors tool;
  DialogColors dialog;

  [[nodiscard]] static const Theme& Instance();
};

[[nodiscard]] Theme CatppuccinMocha();

}  // namespace yac::presentation::theme
