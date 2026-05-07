#include "presentation/markdown/ast.hpp"
#include "presentation/markdown/parser.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace yac::presentation;
using namespace yac::presentation::markdown;

namespace {

const Heading* AsHeading(const BlockNode& node) {
  return std::get_if<Heading>(&node);
}
const Paragraph* AsParagraph(const BlockNode& node) {
  return std::get_if<Paragraph>(&node);
}
const CodeBlock* AsCodeBlock(const BlockNode& node) {
  return std::get_if<CodeBlock>(&node);
}
const Blockquote* AsBlockquote(const BlockNode& node) {
  return AsBlock<Blockquote>(node);
}
const UnorderedList* AsUnorderedList(const BlockNode& node) {
  return AsBlock<UnorderedList>(node);
}
const OrderedList* AsOrderedList(const BlockNode& node) {
  return AsBlock<OrderedList>(node);
}
const HorizontalRule* AsHorizontalRule(const BlockNode& node) {
  return std::get_if<HorizontalRule>(&node);
}
const Table* AsTable(const BlockNode& node) {
  return std::get_if<Table>(&node);
}
const Text* AsText(const InlineNode& node) {
  return std::get_if<Text>(&node);
}
const Bold* AsBold(const InlineNode& node) {
  return std::get_if<Bold>(&node);
}
const Italic* AsItalic(const InlineNode& node) {
  return std::get_if<Italic>(&node);
}
const Strikethrough* AsStrikethrough(const InlineNode& node) {
  return std::get_if<Strikethrough>(&node);
}
const InlineCode* AsInlineCode(const InlineNode& node) {
  return std::get_if<InlineCode>(&node);
}
const Link* AsLink(const InlineNode& node) {
  return std::get_if<Link>(&node);
}
const Image* AsImage(const InlineNode& node) {
  return std::get_if<Image>(&node);
}

}  // namespace

TEST_CASE("Table with simple 2x2 layout") {
  auto blocks = MarkdownParser::Parse("| A | B |\n| - | - |\n| 1 | 2 |");
  REQUIRE(blocks.size() == 1);
  const auto* tbl = AsTable(blocks[0]);
  REQUIRE(tbl != nullptr);
  REQUIRE(tbl->columns.size() == 2);
  REQUIRE(tbl->header.size() == 2);
  REQUIRE(tbl->rows.size() == 1);
  REQUIRE(tbl->header[0].size() == 1);
  const auto* ht = AsText(tbl->header[0][0]);
  REQUIRE(ht != nullptr);
  REQUIRE(ht->content == "A");
  REQUIRE(tbl->rows[0].size() == 2);
  const auto* ct = AsText(tbl->rows[0][0][0]);
  REQUIRE(ct != nullptr);
  REQUIRE(ct->content == "1");
}

TEST_CASE("Table parses column alignment") {
  auto blocks = MarkdownParser::Parse(
      "| A | B | C |\n| :-- | :-: | --: |\n| x | y | z |");
  const auto* tbl = AsTable(blocks[0]);
  REQUIRE(tbl != nullptr);
  REQUIRE(tbl->columns.size() == 3);
  REQUIRE(tbl->columns[0] == ColumnAlignment::Left);
  REQUIRE(tbl->columns[1] == ColumnAlignment::Center);
  REQUIRE(tbl->columns[2] == ColumnAlignment::Right);
}

TEST_CASE("Table cells parse inline formatting") {
  auto blocks =
      MarkdownParser::Parse("| **bold** | `code` |\n| - | - |\n| x | y |");
  const auto* tbl = AsTable(blocks[0]);
  REQUIRE(tbl != nullptr);
  REQUIRE(tbl->header[0].size() == 1);
  const auto* b = AsBold(tbl->header[0][0]);
  REQUIRE(b != nullptr);
  REQUIRE(b->content == "bold");
  const auto* ic = AsInlineCode(tbl->header[1][0]);
  REQUIRE(ic != nullptr);
  REQUIRE(ic->content == "code");
}

TEST_CASE("Table cell escapes pipe") {
  auto blocks = MarkdownParser::Parse("| a \\| b | c |\n| - | - |\n| 1 | 2 |");
  const auto* tbl = AsTable(blocks[0]);
  REQUIRE(tbl != nullptr);
  REQUIRE(tbl->header.size() == 2);
  REQUIRE(tbl->header[0].size() == 1);
  const auto* t = AsText(tbl->header[0][0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "a | b");
}

TEST_CASE("Missing delimiter does not parse as table") {
  auto blocks = MarkdownParser::Parse("| A | B |\nnot a delim");
  REQUIRE(AsTable(blocks[0]) == nullptr);
  REQUIRE(AsParagraph(blocks[0]) != nullptr);
}

TEST_CASE("Table truncates overly wide data rows") {
  auto blocks = MarkdownParser::Parse("| A | B |\n| - | - |\n| 1 | 2 | 3 |");
  const auto* tbl = AsTable(blocks[0]);
  REQUIRE(tbl != nullptr);
  REQUIRE(tbl->rows.size() == 1);
  REQUIRE(tbl->rows[0].size() == 2);
}

TEST_CASE("Table pads short data rows") {
  auto blocks = MarkdownParser::Parse("| A | B |\n| - | - |\n| 1 |");
  const auto* tbl = AsTable(blocks[0]);
  REQUIRE(tbl != nullptr);
  REQUIRE(tbl->rows.size() == 1);
  REQUIRE(tbl->rows[0].size() == 2);
  REQUIRE(tbl->rows[0][1].empty());
}

TEST_CASE("Table without outer pipes is accepted") {
  auto blocks = MarkdownParser::Parse("A | B\n- | -\n1 | 2");
  REQUIRE(blocks.size() == 1);
  const auto* tbl = AsTable(blocks[0]);
  REQUIRE(tbl != nullptr);
  REQUIRE(tbl->columns.size() == 2);
  REQUIRE(tbl->rows.size() == 1);
}

TEST_CASE("Blank line terminates table") {
  auto blocks =
      MarkdownParser::Parse("| A | B |\n| - | - |\n| 1 | 2 |\n\nafter");
  REQUIRE(blocks.size() == 2);
  REQUIRE(AsTable(blocks[0]) != nullptr);
  REQUIRE(AsParagraph(blocks[1]) != nullptr);
}

TEST_CASE("Table with header-only body is valid") {
  auto blocks = MarkdownParser::Parse("| A | B |\n| - | - |");
  REQUIRE(blocks.size() == 1);
  const auto* tbl = AsTable(blocks[0]);
  REQUIRE(tbl != nullptr);
  REQUIRE(tbl->rows.empty());
  REQUIRE(tbl->header.size() == 2);
}

TEST_CASE("Column count mismatch rejects table") {
  auto blocks = MarkdownParser::Parse("| A | B |\n| - | - | - |\n| 1 | 2 |");
  REQUIRE(AsTable(blocks[0]) == nullptr);
}
