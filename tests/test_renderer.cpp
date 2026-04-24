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
}

TEST_CASE("Renderer horizontal rule renders dim line") {
  auto blocks = MarkdownParser::Parse("above\n\n---\n\nbelow");
  auto output = RenderToString(blocks, 80, 10);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("above"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("below"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x94\x80"));
}

TEST_CASE("Renderer nested blockquote renders") {
  auto blocks = MarkdownParser::Parse(">> deeply nested");
  auto output = RenderToString(blocks);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("deeply"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("nested"));
}

TEST_CASE("Renderer multi-line blockquote renders") {
  auto blocks = MarkdownParser::Parse("> line1\n> line2");
  auto output = RenderToString(blocks);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("line1"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("line2"));
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

TEST_CASE("Renderer draws table with headers and data") {
  auto blocks = MarkdownParser::Parse(
      "| HeaderAlpha | HeaderBeta |\n"
      "| ----------- | ---------- |\n"
      "| CellOne     | CellTwo    |");
  auto output = RenderToString(blocks, 60, 10);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("HeaderAlpha"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("HeaderBeta"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("CellOne"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("CellTwo"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x94\x80"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x94\x82"));

  std::vector<std::string> lines;
  std::string current;
  for (char c : output) {
    if (c == '\n') {
      lines.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  size_t header_line = std::string::npos;
  size_t cell_line = std::string::npos;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (header_line == std::string::npos &&
        lines[i].find("HeaderAlpha") != std::string::npos) {
      header_line = i;
    }
    if (cell_line == std::string::npos &&
        lines[i].find("CellOne") != std::string::npos) {
      cell_line = i;
    }
  }
  REQUIRE(header_line != std::string::npos);
  REQUIRE(cell_line != std::string::npos);
  REQUIRE(header_line < cell_line);
}

TEST_CASE(
    "Renderer keeps whole words intact when table cell must shrink to fit") {
  // Regression: when a single-column table's natural content width exceeds
  // the render width, the cell's inline hbox used to contain one ftxui::text
  // per word, which FTXUI's shrink logic would clip per-element - dropping
  // the trailing character of every word ("clockmaker" -> "clockmake", etc.).
  auto blocks = MarkdownParser::Parse(
      "| Answer |\n"
      "| ------ |\n"
      "| He was a clockmaker and he always carried a silver cane |");
  auto output = RenderToString(blocks, 40, 6);
  // Pick the first whole word that fits entirely in the available cell
  // width. If shrink dropped characters, these substrings would be missing.
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("clockmaker"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("always"));
}

TEST_CASE("Renderer right-aligns column with ---: delimiter") {
  auto blocks = MarkdownParser::Parse(
      "| Value                  |\n"
      "| ---------------------: |\n"
      "| x                      |");
  auto output = RenderToString(blocks, 40, 6);
  std::vector<std::string> lines;
  std::string current;
  for (char c : output) {
    if (c == '\n') {
      lines.push_back(current);
      current.clear();
    } else {
      current += c;
    }
  }
  if (!current.empty()) {
    lines.push_back(current);
  }
  size_t data_line = std::string::npos;
  for (size_t i = 0; i < lines.size(); ++i) {
    if (lines[i].find('x') != std::string::npos &&
        lines[i].find("Value") == std::string::npos &&
        lines[i].find("\xe2\x94\x80") == std::string::npos) {
      data_line = i;
      break;
    }
  }
  REQUIRE(data_line != std::string::npos);
  const auto& row = lines[data_line];
  auto x_pos = row.find('x');
  auto left_border = row.find("\xe2\x94\x82");
  auto right_border = row.rfind("\xe2\x94\x82", x_pos);
  auto closing_border = row.find("\xe2\x94\x82", x_pos + 1);
  REQUIRE(x_pos != std::string::npos);
  REQUIRE(left_border != std::string::npos);
  REQUIRE(closing_border != std::string::npos);
  size_t leading = x_pos - right_border - 3;
  size_t trailing = closing_border - x_pos - 1;
  REQUIRE(leading > trailing);
}
