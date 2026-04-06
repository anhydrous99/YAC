#include "presentation/markdown/ast.hpp"
#include "presentation/markdown/parser.hpp"
#include "presentation/markdown/renderer.hpp"
#include "presentation/syntax/highlighter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;
using namespace yac::presentation::markdown;
using namespace yac::presentation::syntax;

namespace {

void RenderAndCheck(const std::vector<BlockNode>& blocks, int width = 80,
                    int height = 24) {
  MarkdownRenderer renderer;
  auto elem = renderer.Render(blocks);
  REQUIRE(elem != nullptr);
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, elem);
  REQUIRE_FALSE(screen.ToString().empty());
}

}  // namespace

TEST_CASE("Renderer handles empty block list") {
  std::vector<BlockNode> blocks;
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles paragraph") {
  MarkdownParser parser;
  auto blocks = parser.Parse("Hello world");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles heading") {
  MarkdownParser parser;
  auto blocks = parser.Parse("# Title");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles code block") {
  MarkdownParser parser;
  auto blocks = parser.Parse("```cpp\nint x = 0;\n```");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles blockquote") {
  MarkdownParser parser;
  auto blocks = parser.Parse("> A wise quote");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles unordered list") {
  MarkdownParser parser;
  auto blocks = parser.Parse("- first\n- second\n- third");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles ordered list") {
  MarkdownParser parser;
  auto blocks = parser.Parse("1. step one\n2. step two");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer handles mixed blocks") {
  MarkdownParser parser;
  auto blocks =
      parser.Parse("# Intro\n\nSome text\n\n- item\n\n```\ncode\n```\n> quote");
  RenderAndCheck(blocks, 80, 48);
}

TEST_CASE("Renderer handles inline formatting") {
  MarkdownParser parser;
  auto blocks = parser.Parse("**bold** and *italic* and `code` and ~~strike~~");
  RenderAndCheck(blocks);
}

TEST_CASE("Renderer paragraph snapshot") {
  MarkdownParser parser;
  auto blocks = parser.Parse("Hello");
  MarkdownRenderer renderer;
  auto elem = renderer.Render(blocks);
  ftxui::Screen screen(80, 1);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Hello"));
}
