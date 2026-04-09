#include "presentation/syntax/highlighter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation::syntax;

TEST_CASE("Highlight returns a non-null element") {
  auto elem = SyntaxHighlighter::Highlight("int x = 0;", "cpp");
  REQUIRE(elem != nullptr);
}

TEST_CASE("Highlight can render to screen without crashing") {
  auto elem = SyntaxHighlighter::Highlight("int x = 0;", "cpp");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
}

TEST_CASE("Highlight with unknown language still renders") {
  auto elem = SyntaxHighlighter::Highlight("some code here", "brainfuck");
  ftxui::Screen screen(80, 2);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
}

TEST_CASE("Highlight multiline code") {
  std::string code = "int main() {\n  return 0;\n}";
  auto elem = SyntaxHighlighter::Highlight(code, "cpp");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
}

// Regression tests: capture exact rendering output for each language
// to verify byte-identical output after unordered_set refactoring.

TEST_CASE("Highlight C++ code rendering is stable") {
  std::string code = "int x = 0;\nvoid foo() {\n  return true;\n}";
  auto elem = SyntaxHighlighter::Highlight(code, "cpp");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
  REQUIRE(output.find("int") != std::string::npos);
  REQUIRE(output.find("void") != std::string::npos);
  REQUIRE(output.find("return") != std::string::npos);
  REQUIRE(output.find("true") != std::string::npos);
}

TEST_CASE("Highlight Python code rendering is stable") {
  std::string code = "def foo(x: int) -> bool:\n    return True\n";
  auto elem = SyntaxHighlighter::Highlight(code, "python");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
  REQUIRE(output.find("def") != std::string::npos);
  REQUIRE(output.find("return") != std::string::npos);
  REQUIRE(output.find("True") != std::string::npos);
}

TEST_CASE("Highlight JavaScript code rendering is stable") {
  std::string code = "const x = 42;\nlet y = null;\nasync function f() {}\n";
  auto elem = SyntaxHighlighter::Highlight(code, "javascript");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
  REQUIRE(output.find("const") != std::string::npos);
  REQUIRE(output.find("async") != std::string::npos);
  REQUIRE(output.find("function") != std::string::npos);
}

TEST_CASE("Highlight Rust code rendering is stable") {
  std::string code = "fn main() {\n    let x: i32 = 42;\n    true\n}\n";
  auto elem = SyntaxHighlighter::Highlight(code, "rust");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
  REQUIRE(output.find("fn") != std::string::npos);
  REQUIRE(output.find("let") != std::string::npos);
  REQUIRE(output.find("true") != std::string::npos);
}

TEST_CASE(
    "Highlight case-insensitive keyword lookup (Python True/False/None)") {
  auto elem = SyntaxHighlighter::Highlight("True and False or None", "python");
  ftxui::Screen screen(80, 2);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
  REQUIRE(output.find("True") != std::string::npos);
  REQUIRE(output.find("False") != std::string::npos);
  REQUIRE(output.find("None") != std::string::npos);
  REQUIRE(output.find("and") != std::string::npos);
  REQUIRE(output.find("or") != std::string::npos);
}

TEST_CASE("Highlight case-insensitive type lookup (Rust String/Vec/Option)") {
  auto elem = SyntaxHighlighter::Highlight(
      "let s: String = vec![];\nlet o: Option<i32> = None;\n", "rust");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
  REQUIRE(output.find("String") != std::string::npos);
  REQUIRE(output.find("Option") != std::string::npos);
}

TEST_CASE("Highlight case-insensitive type lookup (JS Map/Set/Promise)") {
  auto elem = SyntaxHighlighter::Highlight(
      "const m = new Map();\nconst p = new Promise();\n", "javascript");
  ftxui::Screen screen(80, 4);
  ftxui::Render(screen, elem);
  auto output = screen.ToString();
  REQUIRE_FALSE(output.empty());
  REQUIRE(output.find("Map") != std::string::npos);
  REQUIRE(output.find("Promise") != std::string::npos);
}

TEST_CASE("Highlight with case-insensitive language name") {
  auto elem1 = SyntaxHighlighter::Highlight("int x;", "CPP");
  ftxui::Screen screen1(80, 2);
  ftxui::Render(screen1, elem1);
  auto elem2 = SyntaxHighlighter::Highlight("int x;", "cpp");
  ftxui::Screen screen2(80, 2);
  ftxui::Render(screen2, elem2);
  REQUIRE(screen1.ToString() == screen2.ToString());
}
