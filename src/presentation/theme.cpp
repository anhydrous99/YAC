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

  g_theme_registry["catppuccin"] = CatppuccinMocha;
  g_theme_registry["opencode"] = CatppuccinMocha;
  g_theme_registry["system"] = CatppuccinMocha;
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

const Theme& CurrentTheme() {
  if (!g_active_theme.has_value()) {
    g_active_theme = CatppuccinMocha();
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

Theme CatppuccinMocha() {
  Theme t;
  t.name = "catppuccin-mocha";
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

namespace testing {
void ResetThemeForTesting() {
  g_active_theme.reset();
}
}  // namespace testing

}  // namespace yac::presentation::theme
