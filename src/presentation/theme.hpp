#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <ftxui/screen/color.hpp>

namespace yac::presentation::theme {

enum class ThemeDensity { Compact, Comfortable };

struct CanvasRgb {
  std::uint8_t r;
  std::uint8_t g;
  std::uint8_t b;
};

struct Theme;

// Initialize the active theme. Idempotent for the same theme name;
// throws std::logic_error if called with a different name after init.
void InitializeTheme(Theme value);

// Returns the active theme. Lazily initializes to CatppuccinPreset()
// if InitializeTheme has not been called yet.
[[nodiscard]] const Theme& CurrentTheme();

[[nodiscard]] CanvasRgb CurrentCanvasRgb();

using ThemeFactory = std::function<Theme()>;

void RegisterTheme(std::string name, ThemeFactory factory);

[[nodiscard]] Theme GetTheme(const std::string& name);

[[nodiscard]] std::vector<std::string> ListThemes();

struct SemanticRoles {
  ftxui::Color text_strong;
  ftxui::Color text_body;
  ftxui::Color text_weak;
  ftxui::Color text_muted;
  ftxui::Color accent_primary;
  ftxui::Color accent_secondary;
  ftxui::Color surface_canvas;
  ftxui::Color surface_panel;
  ftxui::Color surface_raised;
  ftxui::Color border_subtle;
  ftxui::Color border_strong;
  ftxui::Color focus_ring;
  ftxui::Color selection_bg;
};

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
  CanvasRgb canvas_bg_rgb;
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
  std::string name;
  ThemeDensity density = ThemeDensity::Comfortable;
  SemanticRoles semantic;
  RoleColors role;
  MarkdownColors markdown;
  CodeColors code;
  SyntaxColors syntax;
  ChromeColors chrome;
  CardColors cards;
  ToolColors tool;
  DialogColors dialog;
  SubAgentColors sub_agent;

  [[deprecated(
      "use RenderContext or CurrentTheme()")]] [[nodiscard]] static const Theme&
  Instance() {
    return CurrentTheme();
  }
};

[[nodiscard]] Theme CatppuccinPreset();
[[nodiscard]] Theme OpenCodePreset();
[[nodiscard]] Theme SystemPreset();

}  // namespace yac::presentation::theme
