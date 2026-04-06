#pragma once

#include "../syntax/highlighter.hpp"
#include "ast.hpp"

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::markdown {

class MarkdownRenderer {
 public:
  MarkdownRenderer() = default;

  [[nodiscard]] static ftxui::Element Render(
      const std::vector<BlockNode>& blocks);

 private:
  [[nodiscard]] static ftxui::Element RenderBlock(const BlockNode& block);
  [[nodiscard]] static ftxui::Element RenderInline(
      const std::vector<InlineNode>& nodes);
  [[nodiscard]] static ftxui::Element RenderInlineNode(const InlineNode& node);

  [[nodiscard]] static ftxui::Element RenderHeading(const Heading& h);
  [[nodiscard]] static ftxui::Element RenderParagraph(const Paragraph& p);
  [[nodiscard]] static ftxui::Element RenderCodeBlock(const CodeBlock& cb);
  [[nodiscard]] static ftxui::Element RenderBlockquote(const Blockquote& bq);
  [[nodiscard]] static ftxui::Element RenderUnorderedList(
      const UnorderedList& ul);
  [[nodiscard]] static ftxui::Element RenderOrderedList(const OrderedList& ol);
};

}  // namespace yac::presentation::markdown
