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

TEST_CASE("Empty string yields no blocks") {
  auto blocks = MarkdownParser::Parse("");
  REQUIRE(blocks.empty());
}

TEST_CASE("Whitespace-only string yields no blocks") {
  auto blocks = MarkdownParser::Parse("   \n  \n");
  REQUIRE(blocks.empty());
}

TEST_CASE("ATX heading level 1") {
  auto blocks = MarkdownParser::Parse("# Hello");
  REQUIRE(blocks.size() == 1);
  const auto* h = AsHeading(blocks[0]);
  REQUIRE(h != nullptr);
  REQUIRE(h->level == 1);
  REQUIRE(h->children.size() == 1);
  const auto* t = AsText(h->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "Hello");
}

TEST_CASE("ATX heading level 6") {
  auto blocks = MarkdownParser::Parse("###### Deep");
  REQUIRE(blocks.size() == 1);
  const auto* h = AsHeading(blocks[0]);
  REQUIRE(h != nullptr);
  REQUIRE(h->level == 6);
}

TEST_CASE("Seven hashes is not a heading") {
  auto blocks = MarkdownParser::Parse("####### Not a heading");
  REQUIRE(blocks.size() == 1);
  REQUIRE(AsHeading(blocks[0]) == nullptr);
  REQUIRE(AsParagraph(blocks[0]) != nullptr);
}

TEST_CASE("Hash without trailing space is not a heading") {
  auto blocks = MarkdownParser::Parse("#NoSpace");
  REQUIRE(blocks.size() == 1);
  REQUIRE(AsHeading(blocks[0]) == nullptr);
  REQUIRE(AsParagraph(blocks[0]) != nullptr);
}

TEST_CASE("Fenced code block with backticks and language") {
  auto blocks = MarkdownParser::Parse("```cpp\nint x = 0;\n```");
  REQUIRE(blocks.size() == 1);
  const auto* cb = AsCodeBlock(blocks[0]);
  REQUIRE(cb != nullptr);
  REQUIRE(cb->language == "cpp");
  REQUIRE(cb->source == "int x = 0;");
}

TEST_CASE("Fenced code block with tildes") {
  auto blocks = MarkdownParser::Parse("~~~python\nprint('hi')\n~~~");
  REQUIRE(blocks.size() == 1);
  const auto* cb = AsCodeBlock(blocks[0]);
  REQUIRE(cb != nullptr);
  REQUIRE(cb->language == "python");
  REQUIRE(cb->source == "print('hi')");
}

TEST_CASE("Fenced code block without language") {
  auto blocks = MarkdownParser::Parse("```\nsome code\n```");
  REQUIRE(blocks.size() == 1);
  const auto* cb = AsCodeBlock(blocks[0]);
  REQUIRE(cb != nullptr);
  REQUIRE(cb->language.empty());
  REQUIRE(cb->source == "some code");
}

TEST_CASE("Fenced code block with empty body") {
  auto blocks = MarkdownParser::Parse("```\n```");
  REQUIRE(blocks.size() == 1);
  const auto* cb = AsCodeBlock(blocks[0]);
  REQUIRE(cb != nullptr);
  REQUIRE(cb->source.empty());
}

TEST_CASE("Unclosed code block consumes to end") {
  auto blocks = MarkdownParser::Parse("```\nline1\nline2");
  REQUIRE(blocks.size() == 1);
  const auto* cb = AsCodeBlock(blocks[0]);
  REQUIRE(cb != nullptr);
  REQUIRE(cb->source == "line1\nline2");
}

TEST_CASE("Blockquote with space after >") {
  auto blocks = MarkdownParser::Parse("> quoted text");
  REQUIRE(blocks.size() == 1);
  const auto* bq = AsBlockquote(blocks[0]);
  REQUIRE(bq != nullptr);
  REQUIRE(bq->children.size() == 1);
  const auto* p = AsParagraph(bq->children[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "quoted text");
}

TEST_CASE("Blockquote without space after >") {
  auto blocks = MarkdownParser::Parse(">tight");
  REQUIRE(blocks.size() == 1);
  const auto* bq = AsBlockquote(blocks[0]);
  REQUIRE(bq != nullptr);
  REQUIRE(bq->children.size() == 1);
  const auto* p = AsParagraph(bq->children[0]);
  REQUIRE(p != nullptr);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "tight");
}

TEST_CASE("Multi-line blockquote merges into single paragraph") {
  auto blocks = MarkdownParser::Parse("> line1\n> line2\n> line3");
  REQUIRE(blocks.size() == 1);
  const auto* bq = AsBlockquote(blocks[0]);
  REQUIRE(bq != nullptr);
  REQUIRE(bq->children.size() == 1);
  const auto* p = AsParagraph(bq->children[0]);
  REQUIRE(p != nullptr);
}

TEST_CASE("Nested blockquote with >> contains inner blockquote") {
  auto blocks = MarkdownParser::Parse(">> nested");
  REQUIRE(blocks.size() == 1);
  const auto* bq = AsBlockquote(blocks[0]);
  REQUIRE(bq != nullptr);
  REQUIRE(bq->children.size() == 1);
  const auto* inner = AsBlockquote(bq->children[0]);
  REQUIRE(inner != nullptr);
  REQUIRE(inner->children.size() == 1);
  REQUIRE(AsParagraph(inner->children[0]) != nullptr);
}

TEST_CASE("Single-level blockquote has no nested children") {
  auto blocks = MarkdownParser::Parse("> not nested");
  REQUIRE(blocks.size() == 1);
  const auto* bq = AsBlockquote(blocks[0]);
  REQUIRE(bq != nullptr);
  REQUIRE(bq->children.size() == 1);
  REQUIRE(AsBlockquote(bq->children[0]) == nullptr);
}

TEST_CASE("Blockquote stops at non-quote line") {
  auto blocks = MarkdownParser::Parse("> quote\nplain text");
  REQUIRE(blocks.size() == 2);
  REQUIRE(AsBlockquote(blocks[0]) != nullptr);
  REQUIRE(AsParagraph(blocks[1]) != nullptr);
}

TEST_CASE("Unordered list with dash marker") {
  auto blocks = MarkdownParser::Parse("- item1\n- item2\n- item3");
  REQUIRE(blocks.size() == 1);
  const auto* ul = AsUnorderedList(blocks[0]);
  REQUIRE(ul != nullptr);
  REQUIRE(ul->items.size() == 3);
}

TEST_CASE("Unordered list with star marker") {
  auto blocks = MarkdownParser::Parse("* alpha\n* beta");
  REQUIRE(blocks.size() == 1);
  const auto* ul = AsUnorderedList(blocks[0]);
  REQUIRE(ul != nullptr);
  REQUIRE(ul->items.size() == 2);
}

TEST_CASE("Unordered list with plus marker") {
  auto blocks = MarkdownParser::Parse("+ one\n+ two");
  REQUIRE(blocks.size() == 1);
  const auto* ul = AsUnorderedList(blocks[0]);
  REQUIRE(ul != nullptr);
  REQUIRE(ul->items.size() == 2);
}

TEST_CASE("Unordered list item content") {
  auto blocks = MarkdownParser::Parse("- hello world");
  const auto* ul = AsUnorderedList(blocks[0]);
  REQUIRE(ul != nullptr);
  REQUIRE(ul->items[0].children.size() == 1);
  const auto* p = AsParagraph(ul->items[0].children[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "hello world");
}

TEST_CASE("Ordered list with numeric markers") {
  auto blocks = MarkdownParser::Parse("1. first\n2. second\n3. third");
  REQUIRE(blocks.size() == 1);
  const auto* ol = AsOrderedList(blocks[0]);
  REQUIRE(ol != nullptr);
  REQUIRE(ol->items.size() == 3);
}

TEST_CASE("Ordered list with multi-digit numbers") {
  auto blocks = MarkdownParser::Parse("10. item");
  REQUIRE(blocks.size() == 1);
  const auto* ol = AsOrderedList(blocks[0]);
  REQUIRE(ol != nullptr);
  REQUIRE(ol->items.size() == 1);
  REQUIRE(ol->start == 10);
  const auto* p = AsParagraph(ol->items[0].children[0]);
  REQUIRE(p != nullptr);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "item");
}

TEST_CASE("Horizontal rule with dashes") {
  auto blocks = MarkdownParser::Parse("---");
  REQUIRE(blocks.size() == 1);
  REQUIRE(AsHorizontalRule(blocks[0]) != nullptr);
}

TEST_CASE("Horizontal rule with asterisks") {
  auto blocks = MarkdownParser::Parse("***");
  REQUIRE(blocks.size() == 1);
  REQUIRE(AsHorizontalRule(blocks[0]) != nullptr);
}

TEST_CASE("Horizontal rule with underscores") {
  auto blocks = MarkdownParser::Parse("___");
  REQUIRE(blocks.size() == 1);
  REQUIRE(AsHorizontalRule(blocks[0]) != nullptr);
}

TEST_CASE("Horizontal rule with many dashes") {
  auto blocks = MarkdownParser::Parse("----------");
  REQUIRE(blocks.size() == 1);
  REQUIRE(AsHorizontalRule(blocks[0]) != nullptr);
}

TEST_CASE("Two dashes is not a horizontal rule") {
  auto blocks = MarkdownParser::Parse("--");
  REQUIRE(blocks.size() == 1);
  REQUIRE(AsHorizontalRule(blocks[0]) == nullptr);
}

TEST_CASE("Mixed characters is not a horizontal rule") {
  auto blocks = MarkdownParser::Parse("-*-");
  REQUIRE(blocks.size() == 1);
  REQUIRE(AsHorizontalRule(blocks[0]) == nullptr);
}

TEST_CASE("Horizontal rule between paragraphs") {
  auto blocks = MarkdownParser::Parse("above\n\n---\n\nbelow");
  REQUIRE(blocks.size() == 3);
  REQUIRE(AsParagraph(blocks[0]) != nullptr);
  REQUIRE(AsHorizontalRule(blocks[1]) != nullptr);
  REQUIRE(AsParagraph(blocks[2]) != nullptr);
}

TEST_CASE("Plain text is a paragraph") {
  auto blocks = MarkdownParser::Parse("Hello world");
  REQUIRE(blocks.size() == 1);
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "Hello world");
}

TEST_CASE("Consecutive lines join into one paragraph") {
  auto blocks = MarkdownParser::Parse("line one\nline two");
  REQUIRE(blocks.size() == 1);
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 3);
  const auto* t0 = AsText(p->children[0]);
  const auto* t1 = AsText(p->children[1]);
  const auto* t2 = AsText(p->children[2]);
  REQUIRE(t0 != nullptr);
  REQUIRE(t1 != nullptr);
  REQUIRE(t2 != nullptr);
  REQUIRE(t0->content == "line one");
  REQUIRE(t1->content == " ");
  REQUIRE(t2->content == "line two");
}

TEST_CASE("Blank line splits paragraphs") {
  auto blocks = MarkdownParser::Parse("para one\n\npara two");
  REQUIRE(blocks.size() == 2);
  REQUIRE(AsParagraph(blocks[0]) != nullptr);
  REQUIRE(AsParagraph(blocks[1]) != nullptr);
}

TEST_CASE("Mixed block types preserve order") {
  auto blocks =
      MarkdownParser::Parse("# Title\n\n- item\n\n```\ncode\n```\n> quote");
  REQUIRE(blocks.size() == 4);
  REQUIRE(AsHeading(blocks[0]) != nullptr);
  REQUIRE(AsUnorderedList(blocks[1]) != nullptr);
  REQUIRE(AsCodeBlock(blocks[2]) != nullptr);
  REQUIRE(AsBlockquote(blocks[3]) != nullptr);
}

TEST_CASE("Bold **text**") {
  auto blocks = MarkdownParser::Parse("**bold**");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* b = AsBold(p->children[0]);
  REQUIRE(b != nullptr);
  REQUIRE(b->content == "bold");
}

TEST_CASE("Italic *text*") {
  auto blocks = MarkdownParser::Parse("*italic*");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* it = AsItalic(p->children[0]);
  REQUIRE(it != nullptr);
  REQUIRE(it->content == "italic");
}

TEST_CASE("Strikethrough ~~text~~") {
  auto blocks = MarkdownParser::Parse("~~strike~~");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* s = AsStrikethrough(p->children[0]);
  REQUIRE(s != nullptr);
  REQUIRE(s->content == "strike");
}

TEST_CASE("Inline code `text`") {
  auto blocks = MarkdownParser::Parse("`code`");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* ic = AsInlineCode(p->children[0]);
  REQUIRE(ic != nullptr);
  REQUIRE(ic->content == "code");
}

TEST_CASE("Link [text](url)") {
  auto blocks = MarkdownParser::Parse("[click](https://example.com)");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* ln = AsLink(p->children[0]);
  REQUIRE(ln != nullptr);
  REQUIRE(ln->text == "click");
  REQUIRE(ln->url == "https://example.com");
}

TEST_CASE("Unclosed bold delimiter falls back to plain text") {
  auto blocks = MarkdownParser::Parse("**unclosed");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE_THAT(t->content, Catch::Matchers::ContainsSubstring("*"));
}

TEST_CASE("Unclosed inline code is plain text") {
  auto blocks = MarkdownParser::Parse("`unclosed code");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE_THAT(t->content,
               Catch::Matchers::ContainsSubstring("`unclosed code"));
}

TEST_CASE("Multiple inline types in one line") {
  auto blocks = MarkdownParser::Parse("**b** and *i* and `c`");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 5);
  REQUIRE(AsBold(p->children[0]) != nullptr);
  REQUIRE(AsText(p->children[1]) != nullptr);
  REQUIRE(AsItalic(p->children[2]) != nullptr);
  REQUIRE(AsText(p->children[3]) != nullptr);
  REQUIRE(AsInlineCode(p->children[4]) != nullptr);
}

TEST_CASE("Inline formatting inside list items") {
  auto blocks = MarkdownParser::Parse("- **bold item**");
  const auto* ul = AsUnorderedList(blocks[0]);
  REQUIRE(ul != nullptr);
  REQUIRE(ul->items[0].children.size() == 1);
  const auto* p = AsParagraph(ul->items[0].children[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* b = AsBold(p->children[0]);
  REQUIRE(b != nullptr);
  REQUIRE(b->content == "bold item");
}

TEST_CASE("Inline formatting inside heading") {
  auto blocks = MarkdownParser::Parse("# **Bold Title**");
  const auto* h = AsHeading(blocks[0]);
  REQUIRE(h != nullptr);
  REQUIRE(h->children.size() == 1);
  const auto* b = AsBold(h->children[0]);
  REQUIRE(b != nullptr);
  REQUIRE(b->content == "Bold Title");
}

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

TEST_CASE("Backslash escapes asterisk to literal text") {
  auto blocks = MarkdownParser::Parse("\\*not bold\\*");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "*not bold*");
}

TEST_CASE("Backslash escapes backtick") {
  auto blocks = MarkdownParser::Parse("\\`not code\\`");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "`not code`");
}

TEST_CASE("Backslash before non-escapable char is preserved") {
  auto blocks = MarkdownParser::Parse("foo\\zbar");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "foo\\zbar");
}

TEST_CASE("Adjacent bold and italic do not bleed") {
  auto blocks = MarkdownParser::Parse("**bold***italic*");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 2);
  const auto* b = AsBold(p->children[0]);
  REQUIRE(b != nullptr);
  REQUIRE(b->content == "bold");
  const auto* it = AsItalic(p->children[1]);
  REQUIRE(it != nullptr);
  REQUIRE(it->content == "italic");
}

TEST_CASE("Code span containing asterisks stays code") {
  auto blocks = MarkdownParser::Parse("`**not bold**`");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* ic = AsInlineCode(p->children[0]);
  REQUIRE(ic != nullptr);
  REQUIRE(ic->content == "**not bold**");
}

TEST_CASE("Double-backtick code span contains a single backtick") {
  auto blocks = MarkdownParser::Parse("`` a`b ``");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* ic = AsInlineCode(p->children[0]);
  REQUIRE(ic != nullptr);
  REQUIRE(ic->content == "a`b");
}

TEST_CASE("Link with parens in URL is balanced") {
  auto blocks =
      MarkdownParser::Parse("[wiki](https://en.wikipedia.org/wiki/Foo_(bar))");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* ln = AsLink(p->children[0]);
  REQUIRE(ln != nullptr);
  REQUIRE(ln->text == "wiki");
  REQUIRE(ln->url == "https://en.wikipedia.org/wiki/Foo_(bar)");
}

TEST_CASE("Image syntax produces Image node") {
  auto blocks = MarkdownParser::Parse("![logo](https://example.com/logo.png)");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* img = AsImage(p->children[0]);
  REQUIRE(img != nullptr);
  REQUIRE(img->alt == "logo");
  REQUIRE(img->url == "https://example.com/logo.png");
}

TEST_CASE("Autolink in angle brackets") {
  auto blocks = MarkdownParser::Parse("see <https://example.com> for details");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 3);
  const auto* ln = AsLink(p->children[1]);
  REQUIRE(ln != nullptr);
  REQUIRE(ln->url == "https://example.com");
  REQUIRE(ln->text == "https://example.com");
}

TEST_CASE("Bare https URL is autolinked") {
  auto blocks = MarkdownParser::Parse("visit https://example.com today");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  bool found = false;
  for (const auto& node : p->children) {
    const auto* ln = AsLink(node);
    if (ln != nullptr && ln->url == "https://example.com") {
      found = true;
    }
  }
  REQUIRE(found);
}

TEST_CASE("Bare URL trailing punctuation is not part of URL") {
  auto blocks = MarkdownParser::Parse("see https://example.com.");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  const Link* ln = nullptr;
  for (const auto& node : p->children) {
    ln = AsLink(node);
    if (ln != nullptr) {
      break;
    }
  }
  REQUIRE(ln != nullptr);
  REQUIRE(ln->url == "https://example.com");
}

TEST_CASE("Underscore inside word does not start emphasis") {
  auto blocks = MarkdownParser::Parse("snake_case_name");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  REQUIRE(p->children.size() == 1);
  const auto* t = AsText(p->children[0]);
  REQUIRE(t != nullptr);
  REQUIRE(t->content == "snake_case_name");
}

TEST_CASE("Asterisk emphasis works inside words") {
  auto blocks = MarkdownParser::Parse("foo*bar*baz");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  bool found_italic = false;
  for (const auto& node : p->children) {
    const auto* it = AsItalic(node);
    if (it != nullptr && it->content == "bar") {
      found_italic = true;
    }
  }
  REQUIRE(found_italic);
}

TEST_CASE("Strikethrough requires double tilde") {
  auto blocks = MarkdownParser::Parse("~single~ ~~double~~");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  bool found_strike = false;
  for (const auto& node : p->children) {
    const auto* s = AsStrikethrough(node);
    if (s != nullptr && s->content == "double") {
      found_strike = true;
    }
  }
  REQUIRE(found_strike);
}

TEST_CASE("Emphasis with whitespace inside delimiter does not match") {
  auto blocks = MarkdownParser::Parse("a * not emphasis * b");
  const auto* p = AsParagraph(blocks[0]);
  REQUIRE(p != nullptr);
  for (const auto& node : p->children) {
    REQUIRE(AsItalic(node) == nullptr);
    REQUIRE(AsBold(node) == nullptr);
  }
}

TEST_CASE("Nested unordered list parses two levels") {
  auto blocks = MarkdownParser::Parse("- outer\n  - inner1\n  - inner2");
  REQUIRE(blocks.size() == 1);
  const auto* ul = AsUnorderedList(blocks[0]);
  REQUIRE(ul != nullptr);
  REQUIRE(ul->items.size() == 1);
  REQUIRE(ul->items[0].children.size() == 2);
  REQUIRE(AsParagraph(ul->items[0].children[0]) != nullptr);
  const auto* nested = AsUnorderedList(ul->items[0].children[1]);
  REQUIRE(nested != nullptr);
  REQUIRE(nested->items.size() == 2);
}

TEST_CASE("Multi-line list item collects continuation paragraph") {
  auto blocks = MarkdownParser::Parse("- first line\n  second line");
  const auto* ul = AsUnorderedList(blocks[0]);
  REQUIRE(ul != nullptr);
  REQUIRE(ul->items.size() == 1);
  REQUIRE(ul->items[0].children.size() == 1);
  const auto* p = AsParagraph(ul->items[0].children[0]);
  REQUIRE(p != nullptr);
  bool saw_second = false;
  for (const auto& node : p->children) {
    const auto* t = AsText(node);
    if (t != nullptr && t->content.find("second") != std::string::npos) {
      saw_second = true;
    }
  }
  REQUIRE(saw_second);
}

TEST_CASE("Task list checked and unchecked items") {
  auto blocks = MarkdownParser::Parse("- [ ] todo\n- [x] done");
  const auto* ul = AsUnorderedList(blocks[0]);
  REQUIRE(ul != nullptr);
  REQUIRE(ul->items.size() == 2);
  REQUIRE(ul->items[0].task);
  REQUIRE_FALSE(ul->items[0].checked);
  REQUIRE(ul->items[1].task);
  REQUIRE(ul->items[1].checked);
}

TEST_CASE("Blockquote can contain a code block") {
  auto blocks = MarkdownParser::Parse("> ```\n> int x;\n> ```");
  REQUIRE(blocks.size() == 1);
  const auto* bq = AsBlockquote(blocks[0]);
  REQUIRE(bq != nullptr);
  REQUIRE(bq->children.size() == 1);
  const auto* cb = AsCodeBlock(bq->children[0]);
  REQUIRE(cb != nullptr);
  REQUIRE(cb->source == "int x;");
}

TEST_CASE("Blockquote can contain a list") {
  auto blocks = MarkdownParser::Parse("> - one\n> - two");
  const auto* bq = AsBlockquote(blocks[0]);
  REQUIRE(bq != nullptr);
  REQUIRE(bq->children.size() == 1);
  const auto* ul = AsUnorderedList(bq->children[0]);
  REQUIRE(ul != nullptr);
  REQUIRE(ul->items.size() == 2);
}

TEST_CASE("Ordered list preserves starting number") {
  auto blocks = MarkdownParser::Parse("3. three\n4. four");
  const auto* ol = AsOrderedList(blocks[0]);
  REQUIRE(ol != nullptr);
  REQUIRE(ol->start == 3);
  REQUIRE(ol->items.size() == 2);
}
