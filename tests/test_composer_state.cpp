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

TEST_CASE("ComposerState wraps word-spaced content by width") {
  ComposerState composer;
  composer.Content() = "one two three";

  REQUIRE(composer.CalculateHeight(8, 7) == 2);
  REQUIRE(composer.CalculateHeight(8, 5) == 3);
}

TEST_CASE("ComposerState hard-wraps long unbroken content") {
  ComposerState composer;
  composer.Content() = "abcdefg";

  REQUIRE(composer.CalculateHeight(8, 3) == 3);
}

TEST_CASE("ComposerState combines hard newlines with soft wrapping") {
  ComposerState composer;
  composer.Content() = "abcd ef\nz";

  REQUIRE(composer.CalculateHeight(8, 4) == 3);
}

TEST_CASE("ComposerState caps soft-wrapped height") {
  ComposerState composer;
  composer.Content() = "one two three four five";

  REQUIRE(composer.CalculateHeight(2, 5) == 2);
}

TEST_CASE("ComposerState Submit returns content and clears composer") {
  ComposerState composer;
  composer.Content() = "hello";

  auto submitted = composer.Submit();

  REQUIRE(submitted == "hello");
  REQUIRE(composer.Empty());
  REQUIRE(composer.CalculateHeight(8) == 1);
}

TEST_CASE("ComposerState Submit preserves soft-wrapped content") {
  ComposerState composer;
  composer.Content() = "one two three four";

  REQUIRE(composer.CalculateHeight(8, 7) == 3);

  auto submitted = composer.Submit();

  REQUIRE(submitted == "one two three four");
  REQUIRE(composer.Empty());
}
