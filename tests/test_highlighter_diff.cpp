#include "presentation/syntax/highlighter.hpp"

#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation::syntax;

namespace {

TokenKind FirstKind(const std::vector<TokenSpan>& spans) {
  return spans.empty() ? TokenKind::Plain : spans.front().kind;
}

}  // namespace

TEST_CASE("Diff classifier marks file headers as Preprocessor") {
  REQUIRE(FirstKind(SyntaxHighlighter::TokenizeLine(
              "+++ b/file.cpp", "diff")) == TokenKind::Preprocessor);
  REQUIRE(FirstKind(SyntaxHighlighter::TokenizeLine(
              "--- a/file.cpp", "diff")) == TokenKind::Preprocessor);
  REQUIRE(FirstKind(SyntaxHighlighter::TokenizeLine(
              "diff --git a/foo b/foo", "diff")) == TokenKind::Preprocessor);
  REQUIRE(FirstKind(SyntaxHighlighter::TokenizeLine(
              "index 1234abcd..5678ef00 100644", "diff")) ==
          TokenKind::Preprocessor);
}

TEST_CASE("Diff classifier marks hunk header as Type") {
  REQUIRE(FirstKind(SyntaxHighlighter::TokenizeLine(
              "@@ -1,3 +1,4 @@", "diff")) == TokenKind::Type);
}

TEST_CASE("Diff classifier marks added lines as Number") {
  REQUIRE(FirstKind(SyntaxHighlighter::TokenizeLine("+added line", "diff")) ==
          TokenKind::Number);
}

TEST_CASE("Diff classifier marks removed lines as Keyword") {
  REQUIRE(FirstKind(SyntaxHighlighter::TokenizeLine("-removed line", "diff")) ==
          TokenKind::Keyword);
}

TEST_CASE("Diff classifier leaves context lines as Plain") {
  REQUIRE(FirstKind(SyntaxHighlighter::TokenizeLine(" context line", "diff")) ==
          TokenKind::Plain);
}

TEST_CASE("Diff classifier resolves 'patch' alias") {
  // Same backing language definition as 'diff'.
  REQUIRE(FirstKind(SyntaxHighlighter::TokenizeLine("+added", "patch")) ==
          TokenKind::Number);
}
