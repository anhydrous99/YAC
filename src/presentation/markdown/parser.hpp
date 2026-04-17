#pragma once

#include "ast.hpp"

#include <optional>
#include <string>
#include <vector>

namespace yac::presentation::markdown {

struct ParseOptions {
  bool streaming{false};
};

class MarkdownParser {
 public:
  [[nodiscard]] static std::vector<BlockNode> Parse(std::string_view markdown);
  [[nodiscard]] static std::vector<BlockNode> Parse(std::string_view markdown,
                                                   const ParseOptions& opts);

 private:
  static constexpr int kMaxHeadingLevel = 6;

  [[nodiscard]] static std::vector<BlockNode> ParseBlocks(
      const std::vector<std::string>& lines, const ParseOptions& opts);

  [[nodiscard]] static std::optional<Heading> TryParseHeading(
      const std::string& line);
  [[nodiscard]] static std::optional<CodeBlock> TryParseCodeBlock(
      const std::vector<std::string>& lines, size_t& index,
      const ParseOptions& opts);
  [[nodiscard]] static std::optional<Blockquote> TryParseBlockquote(
      const std::vector<std::string>& lines, size_t& index,
      const ParseOptions& opts);
  [[nodiscard]] static std::optional<HorizontalRule> TryParseHorizontalRule(
      const std::string& line);
  [[nodiscard]] static std::optional<BlockNode> TryParseList(
      const std::vector<std::string>& lines, size_t& index,
      const ParseOptions& opts);
  [[nodiscard]] static std::optional<Table> TryParseTable(
      const std::vector<std::string>& lines, size_t& index);

  static size_t TryParseParagraph(const std::vector<std::string>& lines,
                                  size_t start,
                                  std::vector<BlockNode>& blocks);

  [[nodiscard]] static std::vector<InlineNode> ParseInline(
      std::string_view text);
};

}  // namespace yac::presentation::markdown
