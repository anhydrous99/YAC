#pragma once

#include <string>
#include <variant>
#include <vector>

namespace yac::presentation::markdown {

// ---------------------------------------------------------------------------
// Inline nodes — leaf-level rich text inside blocks.
// ---------------------------------------------------------------------------

struct Text {
  std::string content;
};

struct Bold {
  std::string content;
};

struct Italic {
  std::string content;
};

struct Strikethrough {
  std::string content;
};

struct InlineCode {
  std::string content;
};

struct Link {
  std::string text;
  std::string url;
};

using InlineNode =
    std::variant<Text, Bold, Italic, Strikethrough, InlineCode, Link>;

// ---------------------------------------------------------------------------
// Block nodes — top-level structural elements.
// ---------------------------------------------------------------------------

struct Paragraph {
  std::vector<InlineNode> children;
};

struct Heading {
  int level{};  // 1-6
  std::vector<InlineNode> children;
};

struct CodeBlock {
  std::string language;  // empty when no info string
  std::string source;
};

struct Blockquote {
  std::vector<InlineNode> children;
};

struct UnorderedList {
  struct Item {
    std::vector<InlineNode> children;
  };
  std::vector<Item> items;
};

struct OrderedList {
  struct Item {
    std::vector<InlineNode> children;
  };
  std::vector<Item> items;
};

using BlockNode = std::variant<Paragraph, Heading, CodeBlock, Blockquote,
                               UnorderedList, OrderedList>;

}  // namespace yac::presentation::markdown
