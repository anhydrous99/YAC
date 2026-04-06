#pragma once

#include <string>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::syntax {

/// Basic keyword-based syntax highlighter.
/// Returns a vbox of styled text elements (one per source line).
class SyntaxHighlighter {
 public:
  /// Highlight the given source code, detecting the language.
  [[nodiscard]] static ftxui::Element Highlight(std::string_view code,
                                                std::string_view language);
};

}  // namespace yac::presentation::syntax
