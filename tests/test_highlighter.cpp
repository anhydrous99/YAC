#include "presentation/syntax/highlighter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation::syntax;

TEST_CASE("Highlight returns a non-null element") {
  SyntaxHighlighter hl;
  auto elem = hl.Highlight("int x = 0;", "cpp");
  REQUIRE(elem != nullptr);
}

TEST_CASE("Highlight can render to screen without crashing") {
  SyntaxHighlighter hl;
  auto elem = hl.Highlight("int x = 0;", "cpp");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
}

TEST_CASE("Highlight with unknown language still renders") {
  SyntaxHighlighter hl;
  auto elem = hl.Highlight("some code here", "brainfuck");
  ftxui::Screen screen(80, 2);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
}

TEST_CASE("Highlight multiline code") {
  SyntaxHighlighter hl;
  std::string code = "int main() {\n  return 0;\n}";
  auto elem = hl.Highlight(code, "cpp");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
}
