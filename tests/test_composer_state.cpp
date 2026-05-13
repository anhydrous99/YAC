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

TEST_CASE("ComposerState FindAtTokenAtCursor finds @ at start") {
  ComposerState composer;
  composer.Content() = "@foo";
  *composer.CursorPosition() = 4;

  const auto found = composer.FindAtTokenAtCursor();
  REQUIRE(found.has_value());
  REQUIRE(*found == 0);
}

TEST_CASE("ComposerState FindAtTokenAtCursor finds @ after whitespace") {
  ComposerState composer;
  composer.Content() = "hello @foo";
  *composer.CursorPosition() = 10;

  const auto found = composer.FindAtTokenAtCursor();
  REQUIRE(found.has_value());
  REQUIRE(*found == 6);
}

TEST_CASE("ComposerState FindAtTokenAtCursor rejects email-style tokens") {
  ComposerState composer;
  composer.Content() = "email me@host.com";
  *composer.CursorPosition() = 17;

  REQUIRE_FALSE(composer.FindAtTokenAtCursor().has_value());
}

TEST_CASE("ComposerState FindAtTokenAtCursor returns nullopt when cursor 0") {
  ComposerState composer;
  composer.Content() = "@foo";
  *composer.CursorPosition() = 0;

  REQUIRE_FALSE(composer.FindAtTokenAtCursor().has_value());
}

TEST_CASE("ComposerState FindAtTokenAtCursor stops at whitespace") {
  ComposerState composer;
  composer.Content() = "@foo bar";
  *composer.CursorPosition() = 8;  // after "bar"

  // No @ between cursor and the prior space → nullopt.
  REQUIRE_FALSE(composer.FindAtTokenAtCursor().has_value());
}

TEST_CASE("ComposerState AtMenuFilter returns text after @") {
  ComposerState composer;
  composer.Content() = "look @foo";
  *composer.CursorPosition() = 9;
  composer.ActivateAtMenu(5);

  REQUIRE(composer.AtMenuFilter() == "foo");
}

TEST_CASE("ComposerState AtMenuFilter is empty when not active") {
  ComposerState composer;
  composer.Content() = "@foo";
  *composer.CursorPosition() = 4;

  REQUIRE(composer.AtMenuFilter().empty());
}

TEST_CASE("ComposerState InsertMention replaces the @token at cursor") {
  ComposerState composer;
  composer.Content() = "look @fo";
  *composer.CursorPosition() = 8;
  composer.ActivateAtMenu(5);

  composer.InsertMention("foo/bar.cpp");

  REQUIRE(composer.Content() == "look @foo/bar.cpp");
  REQUIRE(*composer.CursorPosition() == 17);
  REQUIRE_FALSE(composer.IsAtMenuActive());
}

TEST_CASE("ComposerState InsertMention works at start of buffer") {
  ComposerState composer;
  composer.Content() = "@RE";
  *composer.CursorPosition() = 3;
  composer.ActivateAtMenu(0);

  composer.InsertMention("README.md");

  REQUIRE(composer.Content() == "@README.md");
  REQUIRE(*composer.CursorPosition() == 10);
}

TEST_CASE("ComposerState Submit resets @-menu state") {
  ComposerState composer;
  composer.Content() = "@foo";
  *composer.CursorPosition() = 4;
  composer.ActivateAtMenu(0);
  composer.SetAtMenuSelectedIndex(3);

  auto submitted = composer.Submit();

  REQUIRE(submitted == "@foo");
  REQUIRE_FALSE(composer.IsAtMenuActive());
  REQUIRE(composer.AtMenuSelectedIndex() == 0);
  REQUIRE(composer.AtTokenStart() == 0);
}
