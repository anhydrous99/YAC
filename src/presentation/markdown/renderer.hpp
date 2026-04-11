#pragma once

#include "../render_context.hpp"
#include "../syntax/highlighter.hpp"
#include "ast.hpp"

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::markdown {

class MarkdownRenderer {
 public:
  MarkdownRenderer() = default;

  [[nodiscard]] static ftxui::Element Render(
      const std::vector<BlockNode>& blocks);
  [[nodiscard]] static ftxui::Element Render(
      const std::vector<BlockNode>& blocks, const RenderContext& context);

 private:
  [[nodiscard]] static ftxui::Element RenderBlock(const BlockNode& block,
                                                  const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderInline(
      const std::vector<InlineNode>& nodes, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderInlineNode(
      const InlineNode& node, const RenderContext& context);

  [[nodiscard]] static ftxui::Element RenderHeading(
      const Heading& h, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderParagraph(
      const Paragraph& p, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderCodeBlock(
      const CodeBlock& cb, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderBlockquote(
      const Blockquote& bq, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderUnorderedList(
      const UnorderedList& ul, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderOrderedList(
      const OrderedList& ol, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderHorizontalRule(
      const RenderContext& context);
};

}  // namespace yac::presentation::markdown
