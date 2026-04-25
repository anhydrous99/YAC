#include "presentation/theme.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

using namespace yac::presentation::theme;

namespace {

bool ColorsEqual(const ftxui::Color& a, const ftxui::Color& b) {
  std::string sa;
  a.PrintTo(sa, false);
  std::string sb;
  b.PrintTo(sb, false);
  return sa == sb;
}

void RequireColorEq(const ftxui::Color& actual, const ftxui::Color& expected) {
  REQUIRE(ColorsEqual(actual, expected));
}

}  // namespace

TEST_CASE("GetTheme populates tool and dialog color groups") {
  const std::string name = GENERATE("vivid");
  const auto t = GetTheme(name);

  REQUIRE_FALSE(ColorsEqual(t.tool.header_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.tool.bash_accent, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.tool.edit_add, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.tool.edit_remove, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.tool.edit_context, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.tool.read_accent, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.tool.grep_accent, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.tool.glob_accent, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.tool.web_accent, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.tool.icon_fg, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.tool.bash_accent, t.tool.edit_add));
  REQUIRE_FALSE(ColorsEqual(t.tool.edit_add, t.tool.edit_remove));
  REQUIRE_FALSE(ColorsEqual(t.tool.read_accent, t.tool.grep_accent));

  REQUIRE_FALSE(ColorsEqual(t.dialog.overlay_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.dialog.selected_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.dialog.selected_fg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.dialog.input_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.dialog.input_fg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.dialog.dim_text, ftxui::Color()));
}
