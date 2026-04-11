#pragma once

#include "presentation/render_context.hpp"

#include <string>
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::syntax {

enum class TokenKind { Plain, Keyword, Type, Number, String, Comment };

struct TokenSpan {
  TokenKind kind = TokenKind::Plain;
  std::string text;
};

/// Basic keyword-based syntax highlighter.
/// Returns a vbox of styled text elements (one per source line).
class SyntaxHighlighter {
 public:
  /// Classify a single source line without producing FTXUI elements.
  [[nodiscard]] static std::vector<TokenSpan> TokenizeLine(
      std::string_view line, std::string_view language);

  /// Highlight the given source code, detecting the language.
  [[nodiscard]] static ftxui::Element Highlight(std::string_view code,
                                                std::string_view language);
  [[nodiscard]] static ftxui::Element Highlight(std::string_view code,
                                                std::string_view language,
                                                const RenderContext& context);
};

}  // namespace yac::presentation::syntax
