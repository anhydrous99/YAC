#include "presentation/file_mention_menu.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using yac::presentation::RenderFileMentionMenu;
using yac::tool_call::FileMentionRow;

namespace {

std::string RenderToString(ftxui::Element element, int width = 80) {
  auto screen = ftxui::Screen(width, 12);
  ftxui::Render(screen, element);
  return screen.ToString();
}

}  // namespace

TEST_CASE("RenderFileMentionMenu renders matching files") {
  std::vector<FileMentionRow> rows = {
      {.relative_path = "src/main.cpp", .size_bytes = 1234},
      {.relative_path = "README.md", .size_bytes = 5678},
  };
  const auto output = RenderToString(RenderFileMentionMenu(rows, 0, 80));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("@src/main.cpp"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("@README.md"));
}

TEST_CASE("RenderFileMentionMenu shows empty-state row for no matches") {
  const auto output = RenderToString(RenderFileMentionMenu({}, 0, 80));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("No matching files"));
}

TEST_CASE("RenderFileMentionMenu shows indexing placeholder when warming") {
  const auto output = RenderToString(
      RenderFileMentionMenu({}, 0, 80, /*indexing=*/true));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Indexing workspace"));
  REQUIRE_THAT(output,
               !Catch::Matchers::ContainsSubstring("No matching files"));
}

TEST_CASE("RenderFileMentionMenu formats sizes") {
  std::vector<FileMentionRow> rows = {
      {.relative_path = "tiny", .size_bytes = 10},
      {.relative_path = "kfile", .size_bytes = 2048},
      {.relative_path = "mfile", .size_bytes = 3UL * 1024UL * 1024UL},
  };
  const auto output = RenderToString(RenderFileMentionMenu(rows, 0, 80));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("10B"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("2K"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("3M"));
}
