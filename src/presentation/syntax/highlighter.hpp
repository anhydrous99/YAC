#pragma once

#include "presentation/render_context.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::syntax {

enum class TokenKind {
  Plain,
  Keyword,
  Type,
  Number,
  String,
  Comment,
  FunctionCall,
  Preprocessor,
  Decorator,
  Variable,
};

struct TokenSpan {
  TokenKind kind = TokenKind::Plain;
  std::string text;
};

/// Stateful syntax highlighter for source code rendered as FTXUI elements.
class SyntaxHighlighter {
 public:
  /// Classify a single source line. Stateless — multi-line constructs that
  /// span this line are tokenized as if the line were self-contained.
  [[nodiscard]] static std::vector<TokenSpan> TokenizeLine(
      std::string_view line, std::string_view language);

  /// Highlight the given source code. Carries lexer state across lines so
  /// /* … */, R"(…)", """…""", and `…` correctly span lines.
  [[nodiscard]] static ftxui::Element Highlight(std::string_view code,
                                                std::string_view language);
  [[nodiscard]] static ftxui::Element Highlight(std::string_view code,
                                                std::string_view language,
                                                const RenderContext& context);

  /// Like Highlight, but returns one element per source line so callers can
  /// interleave a gutter or per-line decoration.
  [[nodiscard]] static std::vector<ftxui::Element> HighlightLines(
      std::string_view code, std::string_view language,
      const RenderContext& context);
};

}  // namespace yac::presentation::syntax
