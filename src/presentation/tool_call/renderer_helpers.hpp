#pragma once

#include "../syntax/internal/lexer.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

struct RenderContext;

namespace theme {
struct Theme;
}

namespace tool_call {

inline constexpr size_t kMaxPreviewRows = 20;

struct LexerHandle {
  std::optional<syntax::internal::Lexer> lexer;
  [[nodiscard]] syntax::internal::Lexer* Get() {
    return lexer.has_value() ? &*lexer : nullptr;
  }
};

[[nodiscard]] LexerHandle MakeLexerForFile(std::string_view filepath);

[[nodiscard]] ftxui::Element RenderCodeText(const std::string& text,
                                            const theme::Theme& theme);
[[nodiscard]] ftxui::Element RenderLabelValue(const std::string& label,
                                              const std::string& value,
                                              const theme::Theme& theme);
[[nodiscard]] ftxui::Element RenderWrappedLine(const std::string& text,
                                               ftxui::Color color);
[[nodiscard]] ftxui::Element RenderContainer(const std::string& icon,
                                             const std::string& label,
                                             ftxui::Color accent,
                                             ftxui::Elements content,
                                             const theme::Theme& theme);
[[nodiscard]] ftxui::Element RenderLines(const std::vector<std::string>& lines,
                                         const theme::Theme& theme,
                                         const std::string& empty_text = "");
void AddOmittedRows(ftxui::Elements& content, size_t total,
                    const theme::Theme& theme);
[[nodiscard]] ftxui::Element RenderError(const std::string& error,
                                         const theme::Theme& theme);
[[nodiscard]] ftxui::Element RenderHighlightedLine(
    syntax::internal::Lexer* lexer, std::string_view content,
    const RenderContext& context, ftxui::Color fallback_fg);

}  // namespace tool_call
}  // namespace yac::presentation
