#include "renderer.hpp"

#include "../theme.hpp"
#include "ftxui/dom/elements.hpp"

#include <string>

namespace yac::presentation::markdown {

ftxui::Element MarkdownRenderer::Render(const std::vector<BlockNode>& blocks) {
  ftxui::Elements elements;
  for (const auto& block : blocks) {
    elements.push_back(RenderBlock(block));
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
          return ftxui::text(n.content) | ftxui::bold |
                 ftxui::bgcolor(theme::KInlineCodeBg);
        } else if constexpr (std::is_same_v<T, Link>) {
          return ftxui::text(n.text) | ftxui::color(theme::KLinkColor);
        } else {
          return ftxui::text("");
        }
      },
      node);
}

ftxui::Element MarkdownRenderer::RenderHeading(const Heading& h) {
  auto prefix = std::string(h.level, '#') + " ";
  return ftxui::hbox(
      {ftxui::text(prefix) | ftxui::bold | ftxui::color(theme::KHeadingColor),
       RenderInline(h.children)});
}

ftxui::Element MarkdownRenderer::RenderParagraph(const Paragraph& p) {
  return RenderInline(p.children);
}

ftxui::Element MarkdownRenderer::RenderCodeBlock(const CodeBlock& cb) {
  auto highlighted =
      syntax::SyntaxHighlighter::Highlight(cb.source, cb.language);

  ftxui::Elements inner;
  if (!cb.language.empty()) {
    inner.push_back(ftxui::text(" " + cb.language) |
                    ftxui::color(theme::KDimText) | ftxui::dim);
  }
  inner.push_back(ftxui::text(""));
  inner.push_back(std::move(highlighted));
  inner.push_back(ftxui::text(""));

  return ftxui::vbox(inner) | ftxui::bgcolor(theme::KCodeBg) |
         ftxui::borderRounded | ftxui::color(theme::KCodeBlockBorder);
}

ftxui::Element MarkdownRenderer::RenderBlockquote(const Blockquote& bq) {
  return ftxui::hbox(
             {ftxui::text("│ ") | ftxui::color(theme::KQuoteBorderColor),
              RenderInline(bq.children)}) |
         ftxui::bgcolor(theme::KQuoteBg);
}

ftxui::Element MarkdownRenderer::RenderUnorderedList(const UnorderedList& ul) {
  ftxui::Elements items;
  for (const auto& item : ul.items) {
    items.push_back(
        ftxui::hbox({ftxui::text("  • "), RenderInline(item.children)}));
  }
  return ftxui::vbox(items);
}

ftxui::Element MarkdownRenderer::RenderOrderedList(const OrderedList& ol) {
  ftxui::Elements items;
  for (size_t i = 0; i < ol.items.size(); ++i) {
    auto num = std::to_string(i + 1) + ". ";
    items.push_back(ftxui::hbox(
        {ftxui::text("  " + num), RenderInline(ol.items[i].children)}));
  }
  return ftxui::vbox(items);
}

}  // namespace yac::presentation::markdown
