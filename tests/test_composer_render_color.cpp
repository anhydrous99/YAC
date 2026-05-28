#include "presentation/chat_ui_composer_render.hpp"
#include "presentation/composer_state.hpp"
#include "presentation/theme.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;

TEST_CASE(
    "RenderWrappedComposerInput: cursor and non-cursor lines share body_text "
    "foreground (focused)") {
  ComposerState composer;
  composer.Content() = "line one\nline two\nline three";
  *composer.CursorPosition() = 14;  // inside "line two"

  constexpr int kWidth = 30;
  auto element = detail::RenderWrappedComposerInput(composer, kWidth, true);
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(kWidth),
                                      ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, element);

  const auto expected_fg = theme::CurrentTheme().chrome.body_text;
  // Row 0 = "line one", row 1 = "line two" (cursor), row 2 = "line three"
  REQUIRE(screen.PixelAt(2, 0).foreground_color == expected_fg);
  REQUIRE(screen.PixelAt(2, 1).foreground_color == expected_fg);
  REQUIRE(screen.PixelAt(2, 2).foreground_color == expected_fg);
}

TEST_CASE(
    "RenderWrappedComposerInput: cursor and non-cursor lines share body_text "
    "foreground (unfocused)") {
  ComposerState composer;
  composer.Content() = "line one\nline two\nline three";
  *composer.CursorPosition() = 14;

  constexpr int kWidth = 30;
  auto element = detail::RenderWrappedComposerInput(composer, kWidth, false);
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(kWidth),
                                      ftxui::Dimension::Fixed(3));
  ftxui::Render(screen, element);

  const auto expected_fg = theme::CurrentTheme().chrome.body_text;
  REQUIRE(screen.PixelAt(2, 0).foreground_color == expected_fg);
  REQUIRE(screen.PixelAt(2, 1).foreground_color == expected_fg);
  REQUIRE(screen.PixelAt(2, 2).foreground_color == expected_fg);
}
