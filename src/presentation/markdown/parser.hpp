#pragma once

#include "ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace yac::presentation::markdown {

/// Parses a markdown string into a list of block-level AST nodes.
class MarkdownParser {
 public:
  /// Parse the given markdown text into block nodes.
  [[nodiscard]] std::vector<BlockNode> Parse(std::string_view markdown) const;

 private:
  // Block-level parsing — each handles one line type.
  [[nodiscard]] std::vector<BlockNode> ParseBlocks(
      const std::vector<std::string>& lines) const;

  [[nodiscard]] std::optional<Heading> TryParseHeading(
      const std::string& line) const;
  [[nodiscard]] static std::optional<CodeBlock> TryParseCodeBlock(
      const std::vector<std::string>& lines, size_t& index);
  [[nodiscard]] std::optional<Blockquote> TryParseBlockquote(
      const std::string& line) const;
  [[nodiscard]] std::optional<UnorderedList::Item> TryParseUnorderedItem(
      const std::string& line) const;
  [[nodiscard]] std::optional<OrderedList::Item> TryParseOrderedItem(
      const std::string& line) const;

  // Inline parsing — breaks a text run into styled segments.
  [[nodiscard]] static std::vector<InlineNode> ParseInline(
      std::string_view text);
};

}  // namespace yac::presentation::markdown
