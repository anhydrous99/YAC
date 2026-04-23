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

  g_theme_registry["catppuccin"] = CatppuccinPreset;
  g_theme_registry["opencode"] = OpenCodePreset;
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
    g_active_theme = CatppuccinPreset();
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
              << "'. Falling back to default 'opencode'.\n";
    return g_theme_registry.at("opencode")();
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

Theme CatppuccinPreset() {
  Theme t;
  t.name = "catppuccin";
  t.density = ThemeDensity::Comfortable;
  t.semantic.text_strong = ftxui::Color::RGB(205, 214, 244);
  t.semantic.text_body = ftxui::Color::RGB(186, 194, 222);
  t.semantic.text_weak = ftxui::Color::RGB(147, 153, 178);
  t.semantic.text_muted = ftxui::Color::RGB(147, 153, 178);
  t.semantic.accent_primary = ftxui::Color::RGB(137, 180, 250);
  t.semantic.accent_secondary = ftxui::Color::RGB(116, 199, 236);
  t.semantic.surface_canvas = ftxui::Color::RGB(24, 24, 37);
  t.semantic.surface_panel = ftxui::Color::RGB(30, 30, 46);
  t.semantic.surface_raised = ftxui::Color::RGB(49, 50, 68);
  t.semantic.border_subtle = ftxui::Color::RGB(49, 50, 68);
  t.semantic.border_strong = ftxui::Color::RGB(69, 71, 90);
  t.semantic.focus_ring = ftxui::Color::RGB(137, 180, 250);
  t.semantic.selection_bg = ftxui::Color::RGB(49, 50, 68);
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
  t.chrome.canvas_bg = ftxui::Color::RGB(17, 17, 27);
  t.chrome.canvas_bg_rgb = {17, 17, 27};

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

  t.sub_agent.pending_bg = ftxui::Color::RGB(69, 71, 90);
  t.sub_agent.running_accent = ftxui::Color::RGB(137, 180, 250);
  t.sub_agent.success_accent = ftxui::Color::RGB(166, 227, 161);
  t.sub_agent.error_accent = ftxui::Color::RGB(243, 139, 168);
  t.sub_agent.timeout_accent = ftxui::Color::RGB(250, 179, 135);
  t.sub_agent.header_bg = ftxui::Color::RGB(41, 44, 60);
  t.sub_agent.icon_fg = ftxui::Color::RGB(180, 190, 254);
  t.sub_agent.progress_fg = ftxui::Color::RGB(116, 199, 236);

  return t;
}

// OpenCode theme: warm-dark, restrained, amber/steel-blue accents.
// Transcript surfaces flatten into canvas (no chat-bubble symmetry).
Theme OpenCodePreset() {
  Theme t;
  t.name = "opencode";
  t.density = ThemeDensity::Comfortable;

  t.role.user = ftxui::Color::RGB(112, 162, 191);   // desaturated steel blue
  t.role.agent = ftxui::Color::RGB(214, 209, 195);  // body text (no green tint)
  t.role.error = ftxui::Color::RGB(207, 95, 73);    // burnt sienna

  t.markdown.heading = ftxui::Color::RGB(232, 226, 209);  // light bone
  t.markdown.link = ftxui::Color::RGB(112, 162, 191);     // same as user role
  t.markdown.quote_bg = ftxui::Color::RGB(20, 20, 25);    // same as user_bg

  t.code.bg = ftxui::Color::RGB(20, 20, 25);
  t.code.alt_bg = ftxui::Color::RGB(25, 25, 30);
  t.code.fg = ftxui::Color::RGB(214, 209, 195);
  t.code.inline_bg = ftxui::Color::RGB(30, 30, 35);
  t.code.inline_fg = ftxui::Color::RGB(207, 142, 60);
  t.code.border = ftxui::Color::RGB(40, 38, 33);

  t.syntax.keyword = ftxui::Color::RGB(189, 147, 249);   // soft purple
  t.syntax.string = ftxui::Color::RGB(166, 209, 137);    // sage
  t.syntax.comment = ftxui::Color::RGB(120, 116, 103);   // dim_text
  t.syntax.number = ftxui::Color::RGB(207, 142, 60);     // amber
  t.syntax.type = ftxui::Color::RGB(232, 200, 99);       // gold
  t.syntax.function = ftxui::Color::RGB(112, 162, 191);  // steel blue

  t.chrome.dim_text = ftxui::Color::RGB(120, 116, 103);
  t.chrome.body_text = ftxui::Color::RGB(214, 209, 195);
  t.chrome.prompt = ftxui::Color::RGB(207, 142, 60);   // amber accent
  t.chrome.canvas_bg = ftxui::Color::RGB(13, 13, 17);  // near-black cool tint
  t.chrome.canvas_bg_rgb = {13, 13, 17};

  t.cards.user_bg = ftxui::Color::RGB(20, 20, 25);   // barely-raised panel
  t.cards.agent_bg = ftxui::Color::RGB(13, 13, 17);  // same as canvas

  t.tool.header_bg = ftxui::Color::RGB(20, 20, 25);
  t.tool.bash_accent = ftxui::Color::RGB(207, 142, 60);
  t.tool.edit_add = ftxui::Color::RGB(166, 209, 137);
  t.tool.edit_remove = ftxui::Color::RGB(207, 95, 73);
  t.tool.edit_context = ftxui::Color::RGB(120, 116, 103);
  t.tool.read_accent = ftxui::Color::RGB(112, 162, 191);
  t.tool.grep_accent = ftxui::Color::RGB(189, 147, 249);
  t.tool.glob_accent = ftxui::Color::RGB(127, 191, 175);  // muted teal
  t.tool.web_accent = ftxui::Color::RGB(112, 162, 191);
  t.tool.icon_fg = ftxui::Color::RGB(214, 209, 195);

  t.dialog.overlay_bg = ftxui::Color::RGB(8, 8, 10);
  t.dialog.selected_bg = ftxui::Color::RGB(40, 38, 33);
  t.dialog.selected_fg = ftxui::Color::RGB(232, 226, 209);
  t.dialog.input_bg = ftxui::Color::RGB(20, 20, 25);
  t.dialog.input_fg = ftxui::Color::RGB(214, 209, 195);
  t.dialog.dim_text = ftxui::Color::RGB(120, 116, 103);

  t.sub_agent.pending_bg = ftxui::Color::RGB(25, 25, 30);
  t.sub_agent.running_accent = ftxui::Color::RGB(207, 142, 60);
  t.sub_agent.success_accent = ftxui::Color::RGB(166, 209, 137);
  t.sub_agent.error_accent = ftxui::Color::RGB(207, 95, 73);
  t.sub_agent.timeout_accent = ftxui::Color::RGB(232, 200, 99);
  t.sub_agent.header_bg = ftxui::Color::RGB(20, 20, 25);
  t.sub_agent.icon_fg = ftxui::Color::RGB(214, 209, 195);
  t.sub_agent.progress_fg = ftxui::Color::RGB(112, 162, 191);

  t.semantic.text_strong = ftxui::Color::RGB(232, 226, 209);
  t.semantic.text_body = ftxui::Color::RGB(214, 209, 195);
  t.semantic.text_weak = ftxui::Color::RGB(168, 161, 145);
  t.semantic.text_muted = ftxui::Color::RGB(120, 116, 103);
  t.semantic.accent_primary = ftxui::Color::RGB(207, 142, 60);     // amber
  t.semantic.accent_secondary = ftxui::Color::RGB(112, 162, 191);  // steel blue
  t.semantic.surface_canvas = ftxui::Color::RGB(13, 13, 17);
  t.semantic.surface_panel = ftxui::Color::RGB(20, 20, 25);
  t.semantic.surface_raised = ftxui::Color::RGB(25, 25, 30);
  t.semantic.border_subtle = ftxui::Color::RGB(30, 30, 35);
  t.semantic.border_strong = ftxui::Color::RGB(40, 38, 33);
  t.semantic.focus_ring = ftxui::Color::RGB(207, 142, 60);
  t.semantic.selection_bg = ftxui::Color::RGB(40, 38, 33);

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
  t.chrome.canvas_bg_rgb = {0, 0, 0};  // Sentinel: skip OSC 11

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
