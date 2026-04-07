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

TEST_CASE("CatppuccinMocha returns fully populated Theme") {
  auto t = CatppuccinMocha();

  REQUIRE_FALSE(ColorsEqual(t.role.user, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.role.agent, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.markdown.heading, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.markdown.link, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.markdown.quote_border, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.markdown.quote_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.markdown.separator, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.code.bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.code.fg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.code.inline_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.code.inline_fg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.code.block_border, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.syntax.keyword, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.string, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.comment, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.number, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.type, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.syntax.function, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.chrome.border, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.chrome.dim_text, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.chrome.body_text, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.chrome.prompt, ftxui::Color()));

  REQUIRE_FALSE(ColorsEqual(t.cards.user_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.cards.agent_bg, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.cards.user_border, ftxui::Color()));
  REQUIRE_FALSE(ColorsEqual(t.cards.agent_border, ftxui::Color()));
}

TEST_CASE("Theme Instance returns consistent reference") {
  const auto& a = Theme::Instance();
  const auto& b = Theme::Instance();
  REQUIRE(&a == &b);
}

TEST_CASE("Theme Instance matches CatppuccinMocha values") {
  const auto& instance = Theme::Instance();
  auto mocha = CatppuccinMocha();

  REQUIRE(ColorsEqual(instance.role.user, mocha.role.user));
  REQUIRE(ColorsEqual(instance.role.agent, mocha.role.agent));
}

TEST_CASE("CatppuccinMocha role colors are distinct") {
  auto t = CatppuccinMocha();
  REQUIRE_FALSE(ColorsEqual(t.role.user, t.role.agent));
}
