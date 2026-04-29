#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

struct RenderContext;

namespace syntax::internal {
class Lexer;
}

namespace theme {
struct Theme;
}

namespace tool_call {

inline constexpr size_t kMaxPreviewRows = 20;

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
    const RenderContext& context, ftxui::Color fallback_fg,
    ftxui::Color tint_bg, bool tint);

}  // namespace tool_call
}  // namespace yac::presentation
