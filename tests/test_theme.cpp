#include "presentation/theme.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation::theme;

namespace {

bool ColorsEqual(const ftxui::Color& a, const ftxui::Color& b) {
  std::string sa;
  a.PrintTo(sa, false);
  std::string sb;
  b.PrintTo(sb, false);
  return sa == sb;
}

}  // namespace

TEST_CASE("CatppuccinPreset returns fully populated Theme") {
  auto t = CatppuccinPreset();

  REQUIRE_FALSE(ColorsEqual(t.role.user, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.role.agent, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.role.error, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.markdown.heading, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.markdown.link, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.markdown.quote_bg, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.code.bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.code.fg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.code.inline_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.code.inline_fg, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.syntax.keyword, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.string, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.comment, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.number, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.type, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.function, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.chrome.dim_text, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.chrome.body_text, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.chrome.prompt, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.chrome.canvas_bg, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.cards.user_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.cards.agent_bg, ftxui::Color()));
}

TEST_CASE("CatppuccinPreset canvas_bg matches stored RGB tuple") {
  auto t = CatppuccinPreset();
  auto expected =
      ftxui::Color::RGB(t.chrome.canvas_bg_rgb.r, t.chrome.canvas_bg_rgb.g,
                        t.chrome.canvas_bg_rgb.b);
  REQUIRE(t.chrome.canvas_bg_rgb.r == 17);
  REQUIRE(t.chrome.canvas_bg_rgb.g == 17);
  REQUIRE(t.chrome.canvas_bg_rgb.b == 27);
  REQUIRE(ColorsEqual(t.chrome.canvas_bg, expected));
}

TEST_CASE("Theme Instance returns consistent reference") {
  const auto& a = CurrentTheme();
  const auto& b = CurrentTheme();
  REQUIRE(&a == &b);
}

TEST_CASE("Theme Instance matches CatppuccinPreset values") {
  const auto& instance = CurrentTheme();
  auto preset = CatppuccinPreset();

  REQUIRE(ColorsEqual(instance.role.user, preset.role.user));
  REQUIRE(ColorsEqual(instance.role.agent, preset.role.agent));
  REQUIRE(ColorsEqual(instance.role.error, preset.role.error));
}

TEST_CASE("CatppuccinPreset role colors are distinct") {
  auto t = CatppuccinPreset();
  REQUIRE_FALSE(ColorsEqual(t.role.user, t.role.agent));
  REQUIRE_FALSE(ColorsEqual(t.role.user, t.role.error));
  REQUIRE_FALSE(ColorsEqual(t.role.agent, t.role.error));
}
