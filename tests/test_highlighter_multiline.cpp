#include "presentation/render_context.hpp"
#include "presentation/syntax/highlighter.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using yac::presentation::RenderContext;
using yac::presentation::syntax::SyntaxHighlighter;

namespace {

std::string Render(const std::string& code, const std::string& lang) {
  RenderContext ctx;
  auto elem = SyntaxHighlighter::Highlight(code, lang, ctx);
  ftxui::Screen screen(80, 16);
  ftxui::Render(screen, elem);
  return screen.ToString();
}

}  // namespace

TEST_CASE("HighlightLines carries C++ block comment across lines") {
  RenderContext ctx;
  auto elements = SyntaxHighlighter::HighlightLines(
      "/* multi\nline\nblock */ int x = 0;\n", "cpp", ctx);
  // util::SplitLines collapses the trailing \n, so 3 source lines.
  REQUIRE(elements.size() == 3);

  auto out = Render("/* multi\nline\nblock */ int x = 0;\n", "cpp");
  REQUIRE_FALSE(out.empty());
  REQUIRE(out.find("/* multi") != std::string::npos);
  REQUIRE(out.find("line") != std::string::npos);
  REQUIRE(out.find("block */") != std::string::npos);
  REQUIRE(out.find("int") != std::string::npos);
}

TEST_CASE("HighlightLines carries C++ raw string across lines") {
  std::string code = "auto s = R\"(line1\nline2\nline3)\";\n";
  auto out = Render(code, "cpp");
  REQUIRE_FALSE(out.empty());
  REQUIRE(out.find("line1") != std::string::npos);
  REQUIRE(out.find("line2") != std::string::npos);
  REQUIRE(out.find("line3") != std::string::npos);
}

TEST_CASE("HighlightLines carries Python triple-quoted string across lines") {
  std::string code = "x = \"\"\"docstring\nspans\nmultiple\nlines\"\"\"\n";
  auto out = Render(code, "python");
  REQUIRE_FALSE(out.empty());
  REQUIRE(out.find("docstring") != std::string::npos);
  REQUIRE(out.find("spans") != std::string::npos);
  REQUIRE(out.find("multiple") != std::string::npos);
}

TEST_CASE("HighlightLines carries JS template literal across lines") {
  std::string code = "const s = `multi\nline\ntemplate`;\n";
  auto out = Render(code, "javascript");
  REQUIRE_FALSE(out.empty());
  REQUIRE(out.find("multi") != std::string::npos);
  REQUIRE(out.find("line") != std::string::npos);
  REQUIRE(out.find("template") != std::string::npos);
}

TEST_CASE("HighlightLines reports one element per source line") {
  RenderContext ctx;
  auto elements =
      SyntaxHighlighter::HighlightLines("a\nb\nc\nd\ne\n", "cpp", ctx);
  REQUIRE(elements.size() == 5);
}

TEST_CASE("HighlightLines falls back gracefully for unknown language") {
  RenderContext ctx;
  auto elements =
      SyntaxHighlighter::HighlightLines("foo\nbar\n", "klingon", ctx);
  REQUIRE(elements.size() == 2);
}
