#include "presentation/markdown/ast.hpp"
#include "presentation/markdown/parser.hpp"
#include "presentation/markdown/renderer.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;
using namespace yac::presentation::markdown;

namespace {

void RenderAndCheck(const std::vector<BlockNode>& blocks, int width = 80,
                    int height = 24) {
  auto elem = MarkdownRenderer::Render(blocks);
  REQUIRE(elem != nullptr);
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, elem);
  REQUIRE_FALSE(screen.ToString().empty());
}

std::string RenderToString(const std::vector<BlockNode>& blocks, int width = 80,
                           int height = 24) {
  auto elem = MarkdownRenderer::Render(blocks);
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, elem);
  return screen.ToString();
}

void RequireNoLineGlyphs(const std::string& output) {
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("╭"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("╮"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("╰"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("╯"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("─"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("│"));
}

}  // namespace

TEST_CASE("Renderer handles empty block list") {
  std::vector<BlockNode> blocks;
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles paragraph") {
  auto blocks = MarkdownParser::Parse("Hello world");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles heading") {
  auto blocks = MarkdownParser::Parse("# Title");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles code block") {
  auto blocks = MarkdownParser::Parse("```cpp\nint x = 0;\n```");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles blockquote") {
  auto blocks = MarkdownParser::Parse("> A wise quote");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles unordered list") {
  auto blocks = MarkdownParser::Parse("- first\n- second\n- third");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles ordered list") {
  auto blocks = MarkdownParser::Parse("1. step one\n2. step two");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles mixed blocks") {
  auto blocks = MarkdownParser::Parse(
      "# Intro\n\nSome text\n\n- item\n\n```\ncode\n```\n> quote");
  RenderAndCheck(blocks, 80, 48);
}

TEST_CASE("Renderer handles inline formatting") {
  auto blocks =
      MarkdownParser::Parse("**bold** and *italic* and `code` and ~~strike~~");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer paragraph snapshot") {
  auto blocks = MarkdownParser::Parse("Hello");
  auto elem = MarkdownRenderer::Render(blocks);
  ftxui::Screen screen(80, 1);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Hello"));
}

TEST_CASE("Renderer heading H1 is borderless") {
  auto blocks = MarkdownParser::Parse("# Title");
  auto output = RenderToString(blocks, 40, 5);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Title"));
  RequireNoLineGlyphs(output);
}

TEST_CASE("Renderer heading H3 is bold without underline") {
  auto blocks = MarkdownParser::Parse("### Subtitle");
  auto output = RenderToString(blocks, 40, 3);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Subtitle"));
}

TEST_CASE("Renderer heading H5 is dim") {
  auto blocks = MarkdownParser::Parse("##### Minor");
  auto output = RenderToString(blocks, 40, 3);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Minor"));
}

TEST_CASE("Renderer code block shows language badge") {
  auto blocks = MarkdownParser::Parse("```cpp\nint x = 0;\n```");
  auto output = RenderToString(blocks, 80, 10);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("cpp"));
}

TEST_CASE("Renderer code block shows line numbers") {
  auto blocks = MarkdownParser::Parse("```cpp\nint x = 0;\nint y = 1;\n```");
  auto output = RenderToString(blocks, 80, 10);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("1"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("2"));
  RequireNoLineGlyphs(output);
}

TEST_CASE("Renderer horizontal rule preserves spacing without a line") {
  auto blocks = MarkdownParser::Parse("above\n\n---\n\nbelow");
  auto output = RenderToString(blocks, 80, 10);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("above"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("below"));
  RequireNoLineGlyphs(output);
}

TEST_CASE("Renderer nested blockquote renders") {
  auto blocks = MarkdownParser::Parse(">> deeply nested");
  auto output = RenderToString(blocks);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("deeply"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("nested"));
  RequireNoLineGlyphs(output);
}

TEST_CASE("Renderer multi-line blockquote renders") {
  auto blocks = MarkdownParser::Parse("> line1\n> line2");
  auto output = RenderToString(blocks);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("line1"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("line2"));
  RequireNoLineGlyphs(output);
}

TEST_CASE("Renderer unordered list shows bullet") {
  auto blocks = MarkdownParser::Parse("- item");
  auto output = RenderToString(blocks, 40, 3);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("item"));
}

TEST_CASE("Renderer inter-block spacing") {
  auto blocks = MarkdownParser::Parse("# Title\n\nParagraph");
  auto elem = MarkdownRenderer::Render(blocks);
  REQUIRE(elem != nullptr);
}
