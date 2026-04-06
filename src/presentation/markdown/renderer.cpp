#include "renderer.hpp"

#include "ftxui/dom/elements.hpp"

#include <string>

namespace yac::presentation::markdown {

MarkdownRenderer::MarkdownRenderer(const syntax::SyntaxHighlighter& highlighter)
    : highlighter_(highlighter) {}

ftxui::Element MarkdownRenderer::Render(
    const std::vector<BlockNode>& blocks) const {
  ftxui::Elements elements;
  for (const auto& block : blocks) {
    elements.push_back(RenderBlock(block));
  }
  return ftxui::vbox(elements);
}

ftxui::Element MarkdownRenderer::RenderBlock(const BlockNode& block) const {
  return std::visit(
      [this](const auto& node) -> ftxui::Element {
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
    const std::vector<InlineNode>& nodes) const {
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
                 ftxui::bgcolor(ftxui::Color::RGB(49, 50, 68));
        } else if constexpr (std::is_same_v<T, Link>) {
          return ftxui::text(n.text) |
                 ftxui::color(ftxui::Color::RGB(116, 199, 236));
        } else {
          return ftxui::text("");
        }
      },
      node);
}

ftxui::Element MarkdownRenderer::RenderHeading(const Heading& h) const {
  auto prefix = std::string(h.level, '#') + " ";
  return ftxui::hbox({ftxui::text(prefix) | ftxui::bold |
                          ftxui::color(ftxui::Color::RGB(205, 214, 244)),
                      RenderInline(h.children)});
}

ftxui::Element MarkdownRenderer::RenderParagraph(const Paragraph& p) const {
  return RenderInline(p.children);
}

ftxui::Element MarkdownRenderer::RenderCodeBlock(const CodeBlock& cb) const {
  auto highlighted = highlighter_.Highlight(cb.source, cb.language);
  return ftxui::vbox({highlighted}) |
         ftxui::bgcolor(ftxui::Color::RGB(30, 30, 46));
}

ftxui::Element MarkdownRenderer::RenderBlockquote(const Blockquote& bq) const {
  return ftxui::hbox(
      {ftxui::text("│ ") | ftxui::color(ftxui::Color::RGB(250, 179, 135)),
       RenderInline(bq.children)});
}

ftxui::Element MarkdownRenderer::RenderUnorderedList(
    const UnorderedList& ul) const {
  ftxui::Elements items;
  for (const auto& item : ul.items) {
    items.push_back(
        ftxui::hbox({ftxui::text("• "), RenderInline(item.children)}));
  }
  return ftxui::vbox(items);
}

ftxui::Element MarkdownRenderer::RenderOrderedList(
    const OrderedList& ol) const {
  ftxui::Elements items;
  for (size_t i = 0; i < ol.items.size(); ++i) {
    auto num = std::to_string(i + 1) + ". ";
    items.push_back(
        ftxui::hbox({ftxui::text(num), RenderInline(ol.items[i].children)}));
  }
  return ftxui::vbox(items);
}

}  // namespace yac::presentation::markdown
