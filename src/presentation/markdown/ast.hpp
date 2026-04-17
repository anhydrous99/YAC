#pragma once

#include <string>
#include <variant>
#include <vector>

namespace yac::presentation::markdown {

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

struct Paragraph {
  std::vector<InlineNode> children;
};

struct Heading {
  int level{};
  std::vector<InlineNode> children;
};

struct CodeBlock {
  std::string language;
  std::string source;
};

struct Blockquote {
  std::vector<std::vector<InlineNode>> lines;
  int nesting_level{};
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

struct HorizontalRule {};

enum class ColumnAlignment { Default, Left, Center, Right };

struct Table {
  using Cell = std::vector<InlineNode>;
  using Row = std::vector<Cell>;
  std::vector<Cell> header;
  std::vector<ColumnAlignment> columns;
  std::vector<Row> rows;
};

using BlockNode =
    std::variant<Paragraph, Heading, CodeBlock, Blockquote, UnorderedList,
                 OrderedList, HorizontalRule, Table>;

}  // namespace yac::presentation::markdown
