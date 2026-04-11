#include "presentation/composer_state.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation;

TEST_CASE("ComposerState starts empty with one-line height") {
  ComposerState composer;

  REQUIRE(composer.Empty());
  REQUIRE(composer.CalculateHeight(8) == 1);
}

TEST_CASE("ComposerState inserts newlines at cursor") {
  ComposerState composer;

  composer.InsertNewline();
  composer.InsertNewline();

  REQUIRE(composer.Content() == "\n\n");
  REQUIRE(composer.CalculateHeight(8) == 3);
}

TEST_CASE("ComposerState caps calculated height") {
  ComposerState composer;

  for (int i = 0; i < 20; ++i) {
    composer.InsertNewline();
  }

  REQUIRE(composer.CalculateHeight(8) == 8);
}

TEST_CASE("ComposerState Submit returns content and clears composer") {
  ComposerState composer;
  composer.Content() = "hello";

  auto submitted = composer.Submit();

  REQUIRE(submitted == "hello");
  REQUIRE(composer.Empty());
  REQUIRE(composer.CalculateHeight(8) == 1);
}
