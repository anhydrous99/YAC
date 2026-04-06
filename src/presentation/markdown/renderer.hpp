#pragma once

#include "../syntax/highlighter.hpp"
#include "ast.hpp"

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::markdown {

/// Renders markdown AST nodes into ftxui Elements.
class MarkdownRenderer {
 public:
  explicit MarkdownRenderer(const syntax::SyntaxHighlighter& highlighter);

  /// Render a list of block nodes into a single element.
  [[nodiscard]] ftxui::Element Render(
      const std::vector<BlockNode>& blocks) const;

 private:
  [[nodiscard]] ftxui::Element RenderBlock(const BlockNode& block) const;
  [[nodiscard]] ftxui::Element RenderInline(
      const std::vector<InlineNode>& nodes) const;
  [[nodiscard]] static ftxui::Element RenderInlineNode(const InlineNode& node);

  [[nodiscard]] ftxui::Element RenderHeading(const Heading& h) const;
  [[nodiscard]] ftxui::Element RenderParagraph(const Paragraph& p) const;
  [[nodiscard]] ftxui::Element RenderCodeBlock(const CodeBlock& cb) const;
  [[nodiscard]] ftxui::Element RenderBlockquote(const Blockquote& bq) const;
  [[nodiscard]] ftxui::Element RenderUnorderedList(
      const UnorderedList& ul) const;
  [[nodiscard]] ftxui::Element RenderOrderedList(const OrderedList& ol) const;

  const syntax::SyntaxHighlighter& highlighter_;
};

}  // namespace yac::presentation::markdown
