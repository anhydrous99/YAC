#include "presentation/theme.hpp"
#include "presentation/theme_testing.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

void ResetThemeState() {
  yac::presentation::theme::testing::ResetThemeForTesting();
}

}  // namespace

TEST_CASE("Registry returns registered themes") {
  ResetThemeState();

  REQUIRE(yac::presentation::theme::ListThemes().size() >= 3);
  const auto opencode = yac::presentation::theme::GetTheme("opencode");
  const auto catppuccin = yac::presentation::theme::GetTheme("catppuccin");
  const auto system = yac::presentation::theme::GetTheme("system");
  REQUIRE_FALSE(opencode.name.empty());
  REQUIRE_FALSE(catppuccin.name.empty());
  REQUIRE_FALSE(system.name.empty());
}

TEST_CASE("GetTheme returns default and warns on unknown name") {
  ResetThemeState();

  const auto t = yac::presentation::theme::GetTheme("does-not-exist");
  REQUIRE_FALSE(t.name.empty());
}

TEST_CASE("ListThemes returns all built-in presets") {
  ResetThemeState();

  const auto names = yac::presentation::theme::ListThemes();
  REQUIRE(names.size() >= 3);

  bool has_opencode = false;
  bool has_catppuccin = false;
  bool has_system = false;
  // NOLINTNEXTLINE(modernize-loop-convert)
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto& n = names[i];
    has_opencode = has_opencode || n == "opencode";
    has_catppuccin = has_catppuccin || n == "catppuccin";
    has_system = has_system || n == "system";
  }
  REQUIRE(has_opencode);
  REQUIRE(has_catppuccin);
  REQUIRE(has_system);
}

TEST_CASE("RegisterTheme adds a custom factory") {
  ResetThemeState();

  yac::presentation::theme::RegisterTheme("custom-test", []() {
    return yac::presentation::theme::CatppuccinPreset();
  });
  const auto t = yac::presentation::theme::GetTheme("custom-test");
  REQUIRE_FALSE(t.name.empty());
}
