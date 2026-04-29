#include "renderer_helpers.hpp"

#include "../syntax/language_alias.hpp"
#include "../syntax/language_registry.hpp"
#include "../theme.hpp"
#include "../ui_spacing.hpp"

#include <string>
#include <utility>

namespace yac::presentation::tool_call {

LexerHandle MakeLexerForFile(std::string_view filepath) {
  LexerHandle handle;
  auto language = syntax::LanguageForExtension(filepath);
  if (language.empty()) {
    return handle;
  }
  const auto* lang_def = syntax::FindLanguage(language);
  if (lang_def != nullptr) {
    handle.lexer.emplace(*lang_def);
  }
  return handle;
}

ftxui::Element RenderCodeText(const std::string& text,
                              const theme::Theme& theme) {
  return ftxui::paragraph(text) | ftxui::bgcolor(theme.code.inline_bg) |
         ftxui::color(theme.code.inline_fg);
}

ftxui::Element RenderLabelValue(const std::string& label,
                                const std::string& value,
                                const theme::Theme& theme) {
  return ftxui::hbox(
      {ftxui::text(label) | ftxui::color(theme.semantic.text_muted),
       ftxui::paragraph(value) | ftxui::color(theme.semantic.text_body) |
           ftxui::flex});
}

ftxui::Element RenderWrappedLine(const std::string& text, ftxui::Color color) {
  return ftxui::paragraph(text) | ftxui::color(color);
}

ftxui::Element RenderContainer(const std::string& icon,
                               const std::string& label, ftxui::Color accent,
                               ftxui::Elements content,
                               const theme::Theme& theme) {
  auto header = ftxui::hbox({
      ftxui::text("\xe2\x96\x8c") | ftxui::color(accent),
      ftxui::text(" " + icon + " ") | ftxui::color(theme.semantic.text_muted),
      ftxui::text(label) | ftxui::color(theme.semantic.text_strong),
      ftxui::filler(),
  });

  auto body = ftxui::vbox(std::move(content)) |
              ftxui::color(theme.semantic.text_body) |
              ftxui::bgcolor(theme.semantic.surface_raised);

  const int indent =
      theme.density == theme::ThemeDensity::Compact ? 1 : layout::kCardPadX;
  return ftxui::vbox({
      header,
      ftxui::hbox({ftxui::text(std::string(indent, ' ')), body | ftxui::flex}),
  });
}

ftxui::Element RenderLines(const std::vector<std::string>& lines,
                           const theme::Theme& theme,
                           const std::string& empty_text) {
  if (lines.empty()) {
    return empty_text.empty() ? ftxui::text("")
                              : ftxui::text(empty_text) |
                                    ftxui::color(theme.semantic.text_muted);
  }

  ftxui::Elements elements;
  for (const auto& line : lines) {
    elements.push_back(RenderWrappedLine(line, theme.semantic.text_body));
  }
  return ftxui::vbox(std::move(elements));
}

void AddOmittedRows(ftxui::Elements& content, size_t total,
                    const theme::Theme& theme) {
  if (total <= kMaxPreviewRows) {
    return;
  }
  content.push_back(RenderWrappedLine(
      "... " + std::to_string(total - kMaxPreviewRows) + " more omitted",
      theme.semantic.text_muted));
}

ftxui::Element RenderError(const std::string& error,
                           const theme::Theme& theme) {
  return RenderWrappedLine("Error: " + error, theme.tool.edit_remove);
}

ftxui::Element RenderHighlightedLine(syntax::internal::Lexer* lexer,
                                     std::string_view content,
                                     const RenderContext& context,
                                     ftxui::Color fallback_fg,
                                     ftxui::Color tint_bg, bool tint) {
  if (lexer == nullptr) {
    auto element =
        ftxui::paragraph(std::string(content)) | ftxui::color(fallback_fg);
    if (tint) {
      element = element | ftxui::bgcolor(tint_bg);
    }
    return element | ftxui::flex;
  }
  auto spans = lexer->NextLine(content);
  ftxui::Elements segments;
  segments.reserve(spans.size());
  for (const auto& span : spans) {
    segments.push_back(syntax::internal::RenderToken(span, context));
  }
  auto row = segments.empty() ? ftxui::text("") : ftxui::hbox(segments);
  if (tint) {
    row = row | ftxui::bgcolor(tint_bg);
  }
  return row | ftxui::flex;
}

}  // namespace yac::presentation::tool_call
