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
  // Renders blocks and appends `trailing_inline` to the last block's final
  // inline flow so a streaming cursor stays on the same row as the last
  // rendered text instead of dropping to a new line.
  [[nodiscard]] static ftxui::Element Render(
      const std::vector<BlockNode>& blocks, const RenderContext& context,
      ftxui::Element trailing_inline);

 private:
  [[nodiscard]] static ftxui::Element RenderBlock(
      const BlockNode& block, const RenderContext& context,
      ftxui::Element trailing_inline);
  [[nodiscard]] static ftxui::Element RenderInline(
      const std::vector<InlineNode>& nodes, const RenderContext& context,
      ftxui::Element trailing_inline);
  [[nodiscard]] static ftxui::Elements RenderInlineWords(
      const InlineNode& node, const RenderContext& context);

  [[nodiscard]] static ftxui::Element RenderHeading(
      const Heading& h, const RenderContext& context,
      ftxui::Element trailing_inline);
  [[nodiscard]] static ftxui::Element RenderParagraph(
      const Paragraph& p, const RenderContext& context,
      ftxui::Element trailing_inline);
  [[nodiscard]] static ftxui::Element RenderCodeBlock(
      const CodeBlock& cb, const RenderContext& context,
      ftxui::Element trailing_inline);
  [[nodiscard]] static ftxui::Element RenderBlockquote(
      const Blockquote& bq, const RenderContext& context,
      ftxui::Element trailing_inline);
  [[nodiscard]] static ftxui::Element RenderUnorderedList(
      const UnorderedList& ul, const RenderContext& context,
      ftxui::Element trailing_inline);
  [[nodiscard]] static ftxui::Element RenderOrderedList(
      const OrderedList& ol, const RenderContext& context,
      ftxui::Element trailing_inline);
  [[nodiscard]] static ftxui::Element RenderHorizontalRule(
      const RenderContext& context, ftxui::Element trailing_inline);
};

}  // namespace yac::presentation::markdown
