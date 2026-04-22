#pragma once

#include <cstdint>

#include <ftxui/screen/color.hpp>

namespace yac::presentation::theme {

inline constexpr std::uint8_t kCanvasBgRed = 17;
inline constexpr std::uint8_t kCanvasBgGreen = 17;
inline constexpr std::uint8_t kCanvasBgBlue = 27;

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
  ftxui::Color border;
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
  ftxui::Color canvas_bg;
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

struct SubAgentColors {
  ftxui::Color pending_bg;
  ftxui::Color running_accent;
  ftxui::Color success_accent;
  ftxui::Color error_accent;
  ftxui::Color timeout_accent;
  ftxui::Color header_bg;
  ftxui::Color icon_fg;
  ftxui::Color progress_fg;
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
  SubAgentColors sub_agent;

  [[nodiscard]] static const Theme& Instance();
};

[[nodiscard]] Theme CatppuccinMocha();

}  // namespace yac::presentation::theme
