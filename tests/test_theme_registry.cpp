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

  REQUIRE(yac::presentation::theme::ListThemes().size() >= 2);
  const auto vivid = yac::presentation::theme::GetTheme("vivid");
  const auto system = yac::presentation::theme::GetTheme("system");
  REQUIRE_FALSE(vivid.name.empty());
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
  REQUIRE(names.size() >= 2);

  bool has_vivid = false;
  bool has_system = false;
  // NOLINTNEXTLINE(modernize-loop-convert)
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto& n = names[i];
    has_vivid = has_vivid || n == "vivid";
    has_system = has_system || n == "system";
  }
  REQUIRE(has_vivid);
  REQUIRE(has_system);
}

TEST_CASE("RegisterTheme adds a custom factory") {
  ResetThemeState();

  yac::presentation::theme::RegisterTheme(
      "custom-test", []() { return yac::presentation::theme::VividPreset(); });
  const auto t = yac::presentation::theme::GetTheme("custom-test");
  REQUIRE_FALSE(t.name.empty());
}

TEST_CASE("ReinitializeTheme replaces active theme") {
  yac::presentation::theme::testing::ResetThemeForTesting();
  yac::presentation::theme::InitializeTheme(
      yac::presentation::theme::GetTheme("vivid"));
  REQUIRE(yac::presentation::theme::CurrentTheme().name == "vivid");
  yac::presentation::theme::ReinitializeTheme(
      yac::presentation::theme::GetTheme("system"));
  REQUIRE(yac::presentation::theme::CurrentTheme().name == "system");
}
