#include "renderer.hpp"

#include "../theme.hpp"
#include "ftxui/dom/elements.hpp"
#include "presentation/util/string_util.hpp"

#include <string>

namespace yac::presentation::markdown {

inline const auto& k_theme = theme::Theme::Instance();

ftxui::Element MarkdownRenderer::Render(const std::vector<BlockNode>& blocks) {
  ftxui::Elements elements;
  for (size_t i = 0; i < blocks.size(); ++i) {
    elements.push_back(RenderBlock(blocks[i]));
    if (i + 1 < blocks.size()) {
      elements.push_back(ftxui::text(""));
    }
  }
  return ftxui::vbox(elements);
}

ftxui::Element MarkdownRenderer::RenderBlock(const BlockNode& block) {
  return std::visit(
      [](const auto& node) -> ftxui::Element {
        using T = std::decay_t<decltype(node)>;
        if constexpr (std::is_same_v<T, Heading>) {
          return RenderHeading(node);
        } else if constexpr (std::is_same_v<T, Paragraph>) {
          return RenderParagraph(node);
        } else if constexpr (std::is_same_v<T, CodeBlock>) {
          return RenderCodeBlock(node);
        } else if constexpr (std::is_same_v<T, Blockquote>) {
          return RenderBlockquote(node);
        } else if constexpr (std::is_same_v<T, UnorderedList>) {
          return RenderUnorderedList(node);
        } else if constexpr (std::is_same_v<T, OrderedList>) {
          return RenderOrderedList(node);
        } else if constexpr (std::is_same_v<T, HorizontalRule>) {
          return RenderHorizontalRule();
        } else {
          return ftxui::text("");
        }
      },
      block);
}

ftxui::Element MarkdownRenderer::RenderInline(
    const std::vector<InlineNode>& nodes) {
  ftxui::Elements elements;
  for (const auto& node : nodes) {
    elements.push_back(RenderInlineNode(node));
  }
  return ftxui::hbox(elements);
}

ftxui::Element MarkdownRenderer::RenderInlineNode(const InlineNode& node) {
  return std::visit(
      [](const auto& n) -> ftxui::Element {
        using T = std::decay_t<decltype(n)>;
        if constexpr (std::is_same_v<T, Text>) {
          return ftxui::text(n.content);
        } else if constexpr (std::is_same_v<T, Bold>) {
          return ftxui::text(n.content) | ftxui::bold;
        } else if constexpr (std::is_same_v<T, Italic>) {
          return ftxui::text(n.content) | ftxui::italic;
        } else if constexpr (std::is_same_v<T, Strikethrough>) {
          return ftxui::text(n.content) | ftxui::strikethrough;
        } else if constexpr (std::is_same_v<T, InlineCode>) {
          return ftxui::text(" " + n.content + " ") | ftxui::bold |
                 ftxui::bgcolor(k_theme.code.inline_bg) |
                 ftxui::color(k_theme.code.inline_fg);
        } else if constexpr (std::is_same_v<T, Link>) {
          return ftxui::text(n.text) | ftxui::color(k_theme.markdown.link) |
                 ftxui::underlined;
        } else {
          return ftxui::text("");
        }
      },
      node);
}

ftxui::Element MarkdownRenderer::RenderHeading(const Heading& h) {
  auto inline_elem = RenderInline(h.children);

  if (h.level <= 2) {
    return ftxui::vbox({
        inline_elem | ftxui::bold | ftxui::color(k_theme.markdown.heading),
        ftxui::separator() | ftxui::color(k_theme.markdown.separator),
    });
  }

  if (h.level <= 4) {
    return inline_elem | ftxui::bold | ftxui::color(k_theme.markdown.heading);
  }

  return inline_elem | ftxui::color(k_theme.chrome.dim_text);
}

ftxui::Element MarkdownRenderer::RenderParagraph(const Paragraph& p) {
  return RenderInline(p.children);
}

ftxui::Element MarkdownRenderer::RenderCodeBlock(const CodeBlock& cb) {
  auto code_lines = util::SplitLines(cb.source);
  auto gutter_width = std::to_string(code_lines.size()).size() + 1;

  ftxui::Elements inner;
  if (!cb.language.empty()) {
    inner.push_back(ftxui::text(" " + cb.language + " ") |
                    ftxui::bgcolor(k_theme.code.fg) |
                    ftxui::color(k_theme.code.bg) | ftxui::bold);
    inner.push_back(ftxui::text("  ") | ftxui::bgcolor(k_theme.code.bg));
  }

  auto even_bg = k_theme.code.bg;
  auto odd_bg = ftxui::Color::RGB(34, 34, 50);

  for (size_t i = 0; i < code_lines.size(); ++i) {
    auto num_str = std::to_string(i + 1);
    auto padded_num =
        std::string(gutter_width - num_str.size(), ' ') + num_str + " ";
    auto line_elem =
        syntax::SyntaxHighlighter::Highlight(code_lines[i], cb.language);

    auto bg = (i % 2 == 1) ? odd_bg : even_bg;
    inner.push_back(ftxui::hbox({
        ftxui::text(padded_num) | ftxui::color(k_theme.chrome.dim_text) |
            ftxui::dim | ftxui::bgcolor(bg),
        line_elem | ftxui::bgcolor(bg),
    }));
  }

  return ftxui::vbox(inner) | ftxui::bgcolor(k_theme.code.bg) |
         ftxui::borderRounded | ftxui::color(k_theme.code.block_border);
}

ftxui::Element MarkdownRenderer::RenderBlockquote(const Blockquote& bq) {
  const std::array<ftxui::Color, 4> border_colors = {
      k_theme.markdown.quote_border,
      ftxui::Color::RGB(203, 166, 247),
      ftxui::Color::RGB(137, 180, 250),
      ftxui::Color::RGB(166, 227, 161),
  };

  auto color_idx = static_cast<size_t>(bq.nesting_level) % border_colors.size();
  const auto& border_color = border_colors[color_idx];

  std::string indent(bq.nesting_level * 2, ' ');

  ftxui::Elements line_elements;
  for (const auto& line : bq.lines) {
    line_elements.push_back(
        ftxui::hbox({ftxui::text(indent + "│ ") | ftxui::color(border_color),
                     RenderInline(line)}));
  }

  return ftxui::vbox(line_elements) | ftxui::bgcolor(k_theme.markdown.quote_bg);
}

ftxui::Element MarkdownRenderer::RenderUnorderedList(const UnorderedList& ul) {
  ftxui::Elements items;
  for (size_t i = 0; i < ul.items.size(); ++i) {
    items.push_back(ftxui::hbox(
        {ftxui::text("  "),
         ftxui::text("• ") | ftxui::color(k_theme.role.agent) | ftxui::bold,
         RenderInline(ul.items[i].children)}));
  }
  return ftxui::vbox(items);
}

ftxui::Element MarkdownRenderer::RenderOrderedList(const OrderedList& ol) {
  ftxui::Elements items;
  for (size_t i = 0; i < ol.items.size(); ++i) {
    auto num = std::to_string(i + 1) + ". ";
    items.push_back(ftxui::hbox(
        {ftxui::text("  "),
         ftxui::text(num) | ftxui::color(k_theme.chrome.dim_text) | ftxui::bold,
         RenderInline(ol.items[i].children)}));
  }
  return ftxui::vbox(items);
}

ftxui::Element MarkdownRenderer::RenderHorizontalRule() {
  return ftxui::separator() | ftxui::color(k_theme.markdown.separator);
}

}  // namespace yac::presentation::markdown
