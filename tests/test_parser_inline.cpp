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
