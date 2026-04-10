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

void RequireColorEq(const ftxui::Color& actual, const ftxui::Color& expected) {
  REQUIRE(ColorsEqual(actual, expected));
}

}  // namespace

TEST_CASE("CatppuccinMocha populates tool and dialog color groups") {
  auto t = CatppuccinMocha();

  RequireColorEq(t.tool.border, ftxui::Color::RGB(88, 91, 112));
  RequireColorEq(t.tool.header_bg, ftxui::Color::RGB(49, 50, 68));
  RequireColorEq(t.tool.bash_accent, ftxui::Color::RGB(250, 179, 135));
  RequireColorEq(t.tool.edit_add, ftxui::Color::RGB(166, 227, 161));
  RequireColorEq(t.tool.edit_remove, ftxui::Color::RGB(243, 139, 168));
  RequireColorEq(t.tool.edit_context, ftxui::Color::RGB(205, 214, 244));
  RequireColorEq(t.tool.read_accent, ftxui::Color::RGB(137, 180, 250));
  RequireColorEq(t.tool.grep_accent, ftxui::Color::RGB(203, 166, 247));
  RequireColorEq(t.tool.glob_accent, ftxui::Color::RGB(148, 226, 213));
  RequireColorEq(t.tool.web_accent, ftxui::Color::RGB(137, 180, 250));
  RequireColorEq(t.tool.icon_fg, ftxui::Color::RGB(205, 214, 244));

  RequireColorEq(t.dialog.overlay_bg, ftxui::Color::RGB(17, 17, 27));
  RequireColorEq(t.dialog.border, ftxui::Color::RGB(88, 91, 112));
  RequireColorEq(t.dialog.selected_bg, ftxui::Color::RGB(49, 50, 68));
  RequireColorEq(t.dialog.selected_fg, ftxui::Color::RGB(205, 214, 244));
  RequireColorEq(t.dialog.input_bg, ftxui::Color::RGB(30, 30, 46));
  RequireColorEq(t.dialog.input_fg, ftxui::Color::RGB(205, 214, 244));
  RequireColorEq(t.dialog.dim_text, ftxui::Color::RGB(147, 153, 178));
}
