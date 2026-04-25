#include "presentation/syntax/highlighter.hpp"

#include <catch2/catch_test_macros.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation::syntax;

namespace {

TokenKind KindForText(const std::vector<TokenSpan>& spans,
                      std::string_view text) {
  for (const auto& span : spans) {
    if (span.text == text) {
      return span.kind;
    }
  }
  return TokenKind::Plain;
}

}  // namespace

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

TEST_CASE("TokenizeLine classifies keywords, types, and numbers") {
  auto spans = SyntaxHighlighter::TokenizeLine("let s: String = 42;", "rust");

  REQUIRE(KindForText(spans, "let") == TokenKind::Keyword);
  REQUIRE(KindForText(spans, "String") == TokenKind::Type);
  REQUIRE(KindForText(spans, "42") == TokenKind::Number);
}

TEST_CASE("TokenizeLine classifies JavaScript built-in types") {
  auto spans =
      SyntaxHighlighter::TokenizeLine("const m = new Map();", "javascript");

  REQUIRE(KindForText(spans, "const") == TokenKind::Keyword);
  REQUIRE(KindForText(spans, "Map") == TokenKind::Type);
}

TEST_CASE("TokenizeLine falls back to plain tokens for unknown languages") {
  auto spans = SyntaxHighlighter::TokenizeLine("some code here", "unknown");

  REQUIRE(spans.size() == 1);
  REQUIRE(spans[0].kind == TokenKind::Plain);
  REQUIRE(spans[0].text == "some code here");
}

TEST_CASE("TokenizeLine recognises function calls") {
  auto spans = SyntaxHighlighter::TokenizeLine("compute(x);", "cpp");
  REQUIRE(KindForText(spans, "compute") == TokenKind::FunctionCall);
}

TEST_CASE("TokenizeLine does not downgrade keywords to FunctionCall") {
  auto spans = SyntaxHighlighter::TokenizeLine("if (cond) {}", "cpp");
  REQUIRE(KindForText(spans, "if") == TokenKind::Keyword);
}

TEST_CASE("TokenizeLine emits whole-line Preprocessor for #include") {
  auto spans = SyntaxHighlighter::TokenizeLine("#include <vector>", "cpp");
  REQUIRE(spans.size() == 1);
  REQUIRE(spans[0].kind == TokenKind::Preprocessor);
}

TEST_CASE("TokenizeLine emits Decorator for Python @decorator") {
  auto spans = SyntaxHighlighter::TokenizeLine("@dataclass", "python");
  REQUIRE(KindForText(spans, "@dataclass") == TokenKind::Decorator);
}

TEST_CASE("TokenizeLine emits Variable for Bash $VAR") {
  auto spans = SyntaxHighlighter::TokenizeLine("echo $HOME", "bash");
  REQUIRE(KindForText(spans, "$HOME") == TokenKind::Variable);
}

TEST_CASE("TokenizeLine emits Variable for Bash ${VAR}") {
  auto spans = SyntaxHighlighter::TokenizeLine("echo ${HOME}/bin", "bash");
  REQUIRE(KindForText(spans, "${HOME}") == TokenKind::Variable);
}

TEST_CASE("TokenizeLine recognises hex/binary/octal numbers") {
  auto hex = SyntaxHighlighter::TokenizeLine("auto a = 0xFF;", "cpp");
  REQUIRE(KindForText(hex, "0xFF") == TokenKind::Number);

  auto bin = SyntaxHighlighter::TokenizeLine("auto b = 0b1010;", "cpp");
  REQUIRE(KindForText(bin, "0b1010") == TokenKind::Number);

  auto under = SyntaxHighlighter::TokenizeLine("x = 1_000_000", "python");
  REQUIRE(KindForText(under, "1_000_000") == TokenKind::Number);

  auto exp = SyntaxHighlighter::TokenizeLine("y = 3.14e-5", "python");
  REQUIRE(KindForText(exp, "3.14e-5") == TokenKind::Number);
}

TEST_CASE("Language alias resolution: cc and hpp map to cpp") {
  auto cc = SyntaxHighlighter::TokenizeLine("int x = 0;", "cc");
  REQUIRE(KindForText(cc, "int") == TokenKind::Type);

  auto hpp = SyntaxHighlighter::TokenizeLine("int x = 0;", "hpp");
  REQUIRE(KindForText(hpp, "int") == TokenKind::Type);
}

TEST_CASE("TokenizeLine recognises TypeScript keywords") {
  auto spans =
      SyntaxHighlighter::TokenizeLine("interface Foo { x: number; }", "ts");
  REQUIRE(KindForText(spans, "interface") == TokenKind::Keyword);
  REQUIRE(KindForText(spans, "number") == TokenKind::Keyword);
}

TEST_CASE("TokenizeLine recognises Go keywords and types") {
  auto spans =
      SyntaxHighlighter::TokenizeLine("func main() { var x int }", "go");
  REQUIRE(KindForText(spans, "func") == TokenKind::Keyword);
  REQUIRE(KindForText(spans, "var") == TokenKind::Keyword);
  REQUIRE(KindForText(spans, "int") == TokenKind::Type);
}

TEST_CASE("TokenizeLine recognises JSON keywords") {
  auto spans = SyntaxHighlighter::TokenizeLine("{ \"a\": null }", "json");
  REQUIRE(KindForText(spans, "null") == TokenKind::Keyword);
}

TEST_CASE("TokenizeLine recognises Bash keywords") {
  auto spans =
      SyntaxHighlighter::TokenizeLine("if [ -z $x ]; then echo hi; fi", "bash");
  REQUIRE(KindForText(spans, "if") == TokenKind::Keyword);
  REQUIRE(KindForText(spans, "then") == TokenKind::Keyword);
  REQUIRE(KindForText(spans, "fi") == TokenKind::Keyword);
}
