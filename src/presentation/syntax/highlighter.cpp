#include "highlighter.hpp"

#include "internal/lexer.hpp"
#include "language_registry.hpp"
#include "presentation/util/string_util.hpp"

namespace yac::presentation::syntax {

namespace {

ftxui::Element RenderLine(const std::vector<TokenSpan>& spans,
                          const RenderContext& context) {
  if (spans.empty()) {
    return ftxui::text("");
  }
  ftxui::Elements segments;
  segments.reserve(spans.size());
  for (const auto& span : spans) {
    segments.push_back(internal::RenderToken(span, context));
  }
  return ftxui::hbox(std::move(segments));
}

}  // namespace

std::vector<TokenSpan> SyntaxHighlighter::TokenizeLine(
    std::string_view line, std::string_view language) {
  const auto* lang = FindLanguage(language);
  if (lang == nullptr) {
    return {{TokenKind::Plain, std::string(line)}};
  }
  internal::Lexer lexer(*lang);
  return lexer.NextLine(line);
}

ftxui::Element SyntaxHighlighter::Highlight(std::string_view code,
                                            std::string_view language) {
  return Highlight(code, language, RenderContext{});
}

ftxui::Element SyntaxHighlighter::Highlight(std::string_view code,
                                            std::string_view language,
                                            const RenderContext& context) {
  return ftxui::vbox(HighlightLines(code, language, context));
}

std::vector<ftxui::Element> SyntaxHighlighter::HighlightLines(
    std::string_view code, std::string_view language,
    const RenderContext& context) {
  auto lines = util::SplitLines(code);
  std::vector<ftxui::Element> result;
  result.reserve(lines.size());

  const auto* lang = FindLanguage(language);
  if (lang == nullptr) {
    for (const auto& line : lines) {
      result.push_back(ftxui::text(line));
    }
    return result;
  }

  internal::Lexer lexer(*lang);
  for (const auto& line : lines) {
    result.push_back(RenderLine(lexer.NextLine(line), context));
  }
  return result;
}

}  // namespace yac::presentation::syntax
