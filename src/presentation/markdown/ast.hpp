#pragma once

#include <memory>
#include <optional>
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

struct Image {
  std::string alt;
  std::string url;
};

struct LineBreak {};

using InlineNode = std::variant<Text, Bold, Italic, Strikethrough, InlineCode,
                                Link, Image, LineBreak>;

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
  bool partial{false};
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

// Recursive containers — defined after BlockNode so they may hold it.
struct Blockquote;
struct UnorderedList;
struct OrderedList;

using BlockNode =
    std::variant<Paragraph, Heading, CodeBlock, std::shared_ptr<Blockquote>,
                 std::shared_ptr<UnorderedList>, std::shared_ptr<OrderedList>,
                 HorizontalRule, Table>;

struct Blockquote {
  std::vector<BlockNode> children;
  int nesting_level{};
};

struct UnorderedList {
  struct Item {
    std::vector<BlockNode> children;
    bool task{false};
    bool checked{false};
  };
  std::vector<Item> items;
};

struct OrderedList {
  struct Item {
    std::vector<BlockNode> children;
  };
  std::vector<Item> items;
  int start{1};
};

// Helper accessors that hide the shared_ptr indirection for recursive types.
template <typename T>
const T* AsBlock(const BlockNode& node) {
  if constexpr (std::is_same_v<T, Blockquote> ||
                std::is_same_v<T, UnorderedList> ||
                std::is_same_v<T, OrderedList>) {
    if (const auto* p = std::get_if<std::shared_ptr<T>>(&node)) {
      return p->get();
    }
    return nullptr;
  } else {
    return std::get_if<T>(&node);
  }
}

template <typename Visitor>
auto VisitBlock(const BlockNode& node, Visitor&& v)
    -> decltype(v(std::declval<const Paragraph&>())) {
  return std::visit(
      [&](const auto& alt) -> decltype(v(std::declval<const Paragraph&>())) {
        using AT = std::decay_t<decltype(alt)>;
        if constexpr (std::is_same_v<AT, std::shared_ptr<Blockquote>> ||
                      std::is_same_v<AT, std::shared_ptr<UnorderedList>> ||
                      std::is_same_v<AT, std::shared_ptr<OrderedList>>) {
          return v(*alt);
        } else {
          return v(alt);
        }
      },
      node);
}

inline BlockNode MakeBlock(Blockquote bq) {
  return std::make_shared<Blockquote>(std::move(bq));
}
inline BlockNode MakeBlock(UnorderedList ul) {
  return std::make_shared<UnorderedList>(std::move(ul));
}
inline BlockNode MakeBlock(OrderedList ol) {
  return std::make_shared<OrderedList>(std::move(ol));
}
template <typename T>
BlockNode MakeBlock(T&& value) {
  return BlockNode{std::forward<T>(value)};
}

}  // namespace yac::presentation::markdown
