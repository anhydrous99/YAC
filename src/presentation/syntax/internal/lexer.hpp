#pragma once

#include "../highlighter.hpp"
#include "../language_def.hpp"
#include "presentation/render_context.hpp"

#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::syntax::internal {

// Stateful lexer. Carries comment / multiline-string state across lines so
// callers feeding lines in order get correct highlighting for /* … */,
// R"(…)", """…""", `…`, and similar constructs.
class Lexer {
 public:
  explicit Lexer(const LanguageDef& lang);

  [[nodiscard]] std::vector<TokenSpan> NextLine(std::string_view line);

 private:
  enum class State { Default, BlockComment, MultilineString };

  std::vector<TokenSpan> NextLineDefault(std::string_view line);
  static std::vector<TokenSpan> NextLineDiff(std::string_view line);

  const LanguageDef* lang_;
  State state_ = State::Default;
  const StringRule* active_string_rule_ = nullptr;
};

[[nodiscard]] ftxui::Element RenderToken(const TokenSpan& span,
                                         const RenderContext& context);

}  // namespace yac::presentation::syntax::internal
