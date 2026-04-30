#include "theme.hpp"

#include <iostream>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>

namespace yac::presentation::theme {

static std::optional<Theme>
    g_active_theme;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)
static std::map<std::string, ThemeFactory>
    g_theme_registry;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

static void RegisterBuiltinThemes() {
  static bool registered = false;
  if (registered) {
    return;
  }
  registered = true;

  g_theme_registry["vivid"] = VividPreset;
  g_theme_registry["system"] = SystemPreset;
}

void InitializeTheme(Theme value) {
  if (!g_active_theme.has_value()) {
    g_active_theme = std::move(value);
    return;
  }
  if (g_active_theme->name == value.name) {
    return;
  }
  throw std::logic_error("[theme] re-initialize with different name: was '" +
                         g_active_theme->name + "' attempting '" + value.name +
                         "'");
}

void ReinitializeTheme(Theme value) {
  g_active_theme = std::move(value);
}

const Theme& CurrentTheme() {
  if (!g_active_theme.has_value()) {
    g_active_theme = VividPreset();
  }
  return *g_active_theme;
}

CanvasRgb CurrentCanvasRgb() {
  return CurrentTheme().chrome.canvas_bg_rgb;
}

void RegisterTheme(std::string name, ThemeFactory factory) {
  RegisterBuiltinThemes();
  g_theme_registry[std::move(name)] = std::move(factory);
}

Theme GetTheme(const std::string& name) {
  RegisterBuiltinThemes();
  auto it = g_theme_registry.find(name);
  if (it == g_theme_registry.end()) {
    std::cerr << "[theme] Unknown theme name: '" << name
              << "'. Falling back to default 'vivid'.\n";
    return g_theme_registry.at("vivid")();
  }
  return it->second();
}

std::vector<std::string> ListThemes() {
  RegisterBuiltinThemes();
  std::vector<std::string> names;
  names.reserve(g_theme_registry.size());
  for (const auto& [name, factory] : g_theme_registry) {
    static_cast<void>(factory);
    names.push_back(name);
  }
  return names;
}

// Vivid theme: high-saturation dark canvas with neon accents.
// Electric cyan, hot magenta, acid green, vivid amber, bright coral.
Theme VividPreset() {
  Theme t;
  t.name = "vivid";
  t.density = ThemeDensity::Comfortable;

  t.semantic.text_strong = ftxui::Color::RGB(240, 240, 255);
  t.semantic.text_body = ftxui::Color::RGB(220, 220, 240);
  t.semantic.text_weak = ftxui::Color::RGB(160, 165, 195);
  t.semantic.text_muted = ftxui::Color::RGB(110, 115, 150);
  t.semantic.accent_primary = ftxui::Color::RGB(0, 230, 255);
  t.semantic.accent_secondary = ftxui::Color::RGB(255, 60, 172);
  t.semantic.surface_canvas = ftxui::Color::RGB(10, 10, 18);
  t.semantic.surface_panel = ftxui::Color::RGB(18, 18, 30);
  t.semantic.surface_raised = ftxui::Color::RGB(28, 28, 48);
  t.semantic.border_subtle = ftxui::Color::RGB(40, 42, 70);
  t.semantic.border_strong = ftxui::Color::RGB(65, 68, 110);
  t.semantic.focus_ring = ftxui::Color::RGB(0, 230, 255);
  t.semantic.selection_bg = ftxui::Color::RGB(40, 42, 70);

  t.role.user = ftxui::Color::RGB(80, 170, 255);
  t.role.agent = ftxui::Color::RGB(0, 255, 140);
  t.role.error = ftxui::Color::RGB(255, 70, 70);

  t.markdown.heading = ftxui::Color::RGB(255, 120, 255);
  t.markdown.link = ftxui::Color::RGB(0, 230, 255);
  t.markdown.quote_bg = ftxui::Color::RGB(18, 18, 30);

  t.code.bg = ftxui::Color::RGB(14, 14, 24);
  t.code.alt_bg = ftxui::Color::RGB(20, 20, 34);
  t.code.fg = ftxui::Color::RGB(230, 230, 255);
  t.code.inline_bg = ftxui::Color::RGB(35, 20, 55);
  t.code.inline_fg = ftxui::Color::RGB(255, 110, 200);
  t.code.border = ftxui::Color::RGB(55, 50, 90);

  t.syntax.keyword = ftxui::Color::RGB(255, 60, 172);
  t.syntax.string = ftxui::Color::RGB(0, 255, 140);
  t.syntax.comment = ftxui::Color::RGB(100, 105, 145);
  t.syntax.number = ftxui::Color::RGB(255, 190, 0);
  t.syntax.type = ftxui::Color::RGB(0, 230, 255);
  t.syntax.function = ftxui::Color::RGB(255, 210, 80);

  t.chrome.dim_text = ftxui::Color::RGB(130, 135, 170);
  t.chrome.body_text = ftxui::Color::RGB(220, 220, 240);
  t.chrome.prompt = ftxui::Color::RGB(0, 230, 255);
  t.chrome.canvas_bg = ftxui::Color::RGB(10, 10, 18);
  t.chrome.canvas_bg_rgb = {.r = 10, .g = 10, .b = 18};

  t.cards.user_bg = ftxui::Color::RGB(16, 16, 40);
  t.cards.agent_bg = ftxui::Color::RGB(10, 10, 18);

  t.tool.header_bg = ftxui::Color::RGB(22, 22, 38);
  t.tool.bash_accent = ftxui::Color::RGB(255, 190, 0);
  t.tool.edit_add = ftxui::Color::RGB(0, 255, 140);
  t.tool.edit_remove = ftxui::Color::RGB(255, 70, 100);
  t.tool.edit_context = ftxui::Color::RGB(160, 165, 195);
  t.tool.read_accent = ftxui::Color::RGB(0, 230, 255);
  t.tool.grep_accent = ftxui::Color::RGB(180, 100, 255);
  t.tool.glob_accent = ftxui::Color::RGB(255, 210, 80);
  t.tool.web_accent = ftxui::Color::RGB(255, 130, 60);
  t.tool.icon_fg = ftxui::Color::RGB(230, 230, 255);

  t.dialog.overlay_bg = ftxui::Color::RGB(5, 5, 10);
  t.dialog.selected_bg = ftxui::Color::RGB(40, 42, 70);
  t.dialog.selected_fg = ftxui::Color::RGB(240, 240, 255);
  t.dialog.input_bg = ftxui::Color::RGB(18, 18, 30);
  t.dialog.input_fg = ftxui::Color::RGB(230, 230, 255);
  t.dialog.dim_text = ftxui::Color::RGB(110, 115, 150);

  t.sub_agent.pending_bg = ftxui::Color::RGB(35, 35, 55);
  t.sub_agent.running_accent = ftxui::Color::RGB(0, 230, 255);
  t.sub_agent.success_accent = ftxui::Color::RGB(0, 255, 140);
  t.sub_agent.error_accent = ftxui::Color::RGB(255, 70, 70);
  t.sub_agent.timeout_accent = ftxui::Color::RGB(255, 190, 0);
  t.sub_agent.header_bg = ftxui::Color::RGB(22, 22, 38);
  t.sub_agent.icon_fg = ftxui::Color::RGB(200, 180, 255);
  t.sub_agent.progress_fg = ftxui::Color::RGB(0, 230, 255);

  return t;
}

Theme SystemPreset() {
  Theme t;
  t.name = "system";
  t.density = ThemeDensity::Comfortable;

  t.role.user = ftxui::Color::Blue;
  t.role.agent = ftxui::Color::Default;
  t.role.error = ftxui::Color::Red;

  t.markdown.heading = ftxui::Color::Default;
  t.markdown.link = ftxui::Color::Blue;
  t.markdown.quote_bg = ftxui::Color::Default;

  t.code.bg = ftxui::Color::Default;
  t.code.alt_bg = ftxui::Color::Default;
  t.code.fg = ftxui::Color::Default;
  t.code.inline_bg = ftxui::Color::Default;
  t.code.inline_fg = ftxui::Color::Yellow;
  t.code.border = ftxui::Color::Default;

  t.syntax.keyword = ftxui::Color::Magenta;
  t.syntax.string = ftxui::Color::Green;
  t.syntax.comment = ftxui::Color::GrayDark;
  t.syntax.number = ftxui::Color::Yellow;
  t.syntax.type = ftxui::Color::Cyan;
  t.syntax.function = ftxui::Color::Blue;

  t.chrome.dim_text = ftxui::Color::GrayDark;
  t.chrome.body_text = ftxui::Color::Default;
  t.chrome.prompt = ftxui::Color::Blue;
  t.chrome.canvas_bg = ftxui::Color::Default;
  t.chrome.canvas_bg_rgb = {.r = 0, .g = 0, .b = 0};  // Sentinel: skip OSC 11

  t.cards.user_bg = ftxui::Color::Default;
  t.cards.agent_bg = ftxui::Color::Default;

  t.tool.header_bg = ftxui::Color::Default;
  t.tool.bash_accent = ftxui::Color::Yellow;
  t.tool.edit_add = ftxui::Color::Green;
  t.tool.edit_remove = ftxui::Color::Red;
  t.tool.edit_context = ftxui::Color::GrayDark;
  t.tool.read_accent = ftxui::Color::Blue;
  t.tool.grep_accent = ftxui::Color::Magenta;
  t.tool.glob_accent = ftxui::Color::Cyan;
  t.tool.web_accent = ftxui::Color::Blue;
  t.tool.icon_fg = ftxui::Color::Default;

  t.dialog.overlay_bg = ftxui::Color::Default;
  t.dialog.selected_bg = ftxui::Color::Blue;
  t.dialog.selected_fg = ftxui::Color::Default;
  t.dialog.input_bg = ftxui::Color::Default;
  t.dialog.input_fg = ftxui::Color::Default;
  t.dialog.dim_text = ftxui::Color::GrayDark;

  t.sub_agent.pending_bg = ftxui::Color::Default;
  t.sub_agent.running_accent = ftxui::Color::Blue;
  t.sub_agent.success_accent = ftxui::Color::Green;
  t.sub_agent.error_accent = ftxui::Color::Red;
  t.sub_agent.timeout_accent = ftxui::Color::Yellow;
  t.sub_agent.header_bg = ftxui::Color::Default;
  t.sub_agent.icon_fg = ftxui::Color::Default;
  t.sub_agent.progress_fg = ftxui::Color::Blue;

  t.semantic.text_strong = ftxui::Color::Default;
  t.semantic.text_body = ftxui::Color::Default;
  t.semantic.text_weak = ftxui::Color::GrayDark;
  t.semantic.text_muted = ftxui::Color::GrayDark;
  t.semantic.accent_primary = ftxui::Color::Blue;
  t.semantic.accent_secondary = ftxui::Color::Cyan;
  t.semantic.surface_canvas = ftxui::Color::Default;
  t.semantic.surface_panel = ftxui::Color::Default;
  t.semantic.surface_raised = ftxui::Color::Default;
  t.semantic.border_subtle = ftxui::Color::GrayDark;
  t.semantic.border_strong = ftxui::Color::Default;
  t.semantic.focus_ring = ftxui::Color::Blue;
  t.semantic.selection_bg = ftxui::Color::Blue;

  return t;
}

namespace testing {
void ResetThemeForTesting() {
  g_active_theme.reset();
}
}  // namespace testing

}  // namespace yac::presentation::theme
