#include "parser.hpp"

#include <algorithm>
#include <sstream>
#include <utility>

namespace yac::presentation::markdown {

namespace {

// -------------------------------------------------------------------------
// Helpers
// -------------------------------------------------------------------------

/// Split text into lines, preserving empty lines as empty strings.
std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::string line;
  std::istringstream stream{std::string(text)};
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }
  return lines;
}

/// Trim leading whitespace.
std::string_view LTrim(std::string_view s) {
  const auto pos = s.find_first_not_of(" \t");
  return (pos == std::string_view::npos) ? std::string_view{} : s.substr(pos);
}

/// Trim trailing whitespace.
std::string_view RTrim(std::string_view s) {
  const auto pos = s.find_last_not_of(" \t");
  return (pos == std::string_view::npos) ? std::string_view{}
                                         : s.substr(0, pos + 1);
}

/// Trim both ends.
std::string_view Trim(std::string_view s) {
  return LTrim(RTrim(s));
}

/// Count leading '#' characters (max 6) for headings.
int CountHeadingLevel(std::string_view line) {
  int level = 0;
  for (char c : line) {
    if (c == '#') {
      ++level;
    } else {
      break;
    }
  }
  return (level >= 1 && level <= 6) ? level : 0;
}

/// Check if line starts a fenced code block (``` or ~~~). Returns the fence
/// length (3+) or 0.
int FenceLength(std::string_view line, char fence_char) {
  if (line.empty() || line[0] != fence_char) {
    return 0;
  }
  int count = 0;
  for (char c : line) {
    if (c == fence_char) {
      ++count;
    } else {
      break;
    }
  }
  return (count >= 3) ? count : 0;
}

/// Extract the language tag from a code fence opening line.
std::string ExtractLang(std::string_view line, int fence_len) {
  auto rest = Trim(line.substr(static_cast<size_t>(fence_len)));
  return std::string(rest);
}

}  // namespace

// =========================================================================
// Public API
// =========================================================================

std::vector<BlockNode> MarkdownParser::Parse(std::string_view markdown) const {
  auto lines = SplitLines(markdown);
  return ParseBlocks(lines);
}

// =========================================================================
// Block-level parsing
// =========================================================================

std::vector<BlockNode> MarkdownParser::ParseBlocks(
    const std::vector<std::string>& lines) const {
  std::vector<BlockNode> blocks;

  for (size_t i = 0; i < lines.size(); ++i) {
    const auto& line = lines[i];

    // Skip blank lines between blocks.
    if (Trim(line).empty()) {
      continue;
    }

    // 1. Heading
    if (auto heading = TryParseHeading(line)) {
      blocks.emplace_back(std::move(*heading));
      continue;
    }

    // 2. Fenced code block
    if (auto code = TryParseCodeBlock(lines, i)) {
      blocks.emplace_back(std::move(*code));
      continue;
    }

    // 3. Blockquote
    if (auto bq = TryParseBlockquote(line)) {
      blocks.emplace_back(std::move(*bq));
      continue;
    }

    // 4. Unordered list item
    if (auto item = TryParseUnorderedItem(line)) {
      UnorderedList list;
      list.items.push_back(std::move(*item));
      // Consume consecutive unordered list items.
      while (i + 1 < lines.size()) {
        auto next = TryParseUnorderedItem(lines[i + 1]);
        if (!next) {
          break;
        }
        list.items.push_back(std::move(*next));
        ++i;
      }
      blocks.emplace_back(std::move(list));
      continue;
    }

    // 5. Ordered list item
    if (auto item = TryParseOrderedItem(line)) {
      OrderedList list;
      list.items.push_back(std::move(*item));
      while (i + 1 < lines.size()) {
        auto next = TryParseOrderedItem(lines[i + 1]);
        if (!next) {
          break;
        }
        list.items.push_back(std::move(*next));
        ++i;
      }
      blocks.emplace_back(std::move(list));
      continue;
    }

    // 6. Paragraph — gather consecutive non-empty, non-special lines.
    {
      Paragraph para;
      para.children = ParseInline(line);
      while (i + 1 < lines.size() && !Trim(lines[i + 1]).empty() &&
             !TryParseHeading(lines[i + 1]) &&
             !TryParseBlockquote(lines[i + 1]) &&
             !TryParseUnorderedItem(lines[i + 1]) &&
             !TryParseOrderedItem(lines[i + 1]) &&
             FenceLength(lines[i + 1], '`') == 0 &&
             FenceLength(lines[i + 1], '~') == 0) {
        ++i;
        // Add a space between joined lines.
        para.children.emplace_back(Text{" "});
        auto more = ParseInline(lines[i]);
        para.children.insert(para.children.end(),
                             std::make_move_iterator(more.begin()),
                             std::make_move_iterator(more.end()));
      }
      blocks.emplace_back(std::move(para));
    }
  }

  return blocks;
}

// =========================================================================
// Individual block parsers
// =========================================================================

std::optional<Heading> MarkdownParser::TryParseHeading(
    const std::string& line) const {
  int level = CountHeadingLevel(line);
  if (level == 0) {
    return std::nullopt;
  }
  // Require a space after the '#'s.
  if (static_cast<size_t>(level) >= line.size() || line[level] != ' ') {
    return std::nullopt;
  }
  auto content =
      Trim(std::string_view(line).substr(static_cast<size_t>(level)));
  Heading h;
  h.level = level;
  h.children = ParseInline(content);
  return h;
}

std::optional<CodeBlock> MarkdownParser::TryParseCodeBlock(
    const std::vector<std::string>& lines, size_t& index) {
  const auto& open = lines[index];

  int fence_len = FenceLength(open, '`');
  char fence_char = '`';
  if (fence_len == 0) {
    fence_len = FenceLength(open, '~');
    fence_char = '~';
  }
  if (fence_len == 0) {
    return std::nullopt;
  }

  std::string lang = ExtractLang(open, fence_len);
  std::string source;
  bool closed = false;

  for (size_t j = index + 1; j < lines.size(); ++j) {
    const auto& inner = lines[j];
    if (FenceLength(inner, fence_char) >= fence_len &&
        Trim(inner.substr(static_cast<size_t>(fence_len))).empty()) {
      index = j;
      closed = true;
      break;
    }
    if (!source.empty()) {
      source += '\n';
    }
    source += inner;
  }

  // Unclosed code block — consume to end.
  if (!closed) {
    index = lines.size() - 1;
  }

  CodeBlock cb;
  cb.language = lang;
  cb.source = source;
  return cb;
}

std::optional<Blockquote> MarkdownParser::TryParseBlockquote(
    const std::string& line) const {
  if (line.empty() || line[0] != '>') {
    return std::nullopt;
  }
  auto content = (line.size() > 1 && line[1] == ' ')
                     ? std::string_view(line).substr(2)
                     : std::string_view(line).substr(1);
  Blockquote bq;
  bq.children = ParseInline(content);
  return bq;
}

std::optional<UnorderedList::Item> MarkdownParser::TryParseUnorderedItem(
    const std::string& line) const {
  auto trimmed = LTrim(line);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  char marker = trimmed[0];
  if (marker != '-' && marker != '*' && marker != '+') {
    return std::nullopt;
  }
  if (trimmed.size() < 2 || trimmed[1] != ' ') {
    return std::nullopt;
  }
  auto content = trimmed.substr(2);
  UnorderedList::Item item;
  item.children = ParseInline(content);
  return item;
}

std::optional<OrderedList::Item> MarkdownParser::TryParseOrderedItem(
    const std::string& line) const {
  auto trimmed = LTrim(line);
  if (trimmed.empty()) {
    return std::nullopt;
  }
  // Match one or more digits followed by '.' and space.
  size_t dot_pos = trimmed.find('.');
  if (dot_pos == std::string_view::npos || dot_pos == 0) {
    return std::nullopt;
  }
  for (size_t d = 0; d < dot_pos; ++d) {
    if (std::isdigit(static_cast<unsigned char>(trimmed[d])) == 0) {
      return std::nullopt;
    }
  }
  if (trimmed.size() <= dot_pos + 1 || trimmed[dot_pos + 1] != ' ') {
    return std::nullopt;
  }
  auto content = trimmed.substr(dot_pos + 2);
  OrderedList::Item item;
  item.children = ParseInline(content);
  return item;
}

// =========================================================================
// Inline parsing — single-pass, left-to-right
// =========================================================================

std::vector<InlineNode> MarkdownParser::ParseInline(std::string_view text) {
  std::vector<InlineNode> nodes;
  std::string plain;

  auto flush = [&]() {
    if (!plain.empty()) {
      nodes.emplace_back(Text{std::exchange(plain, {})});
    }
  };

  size_t i = 0;
  while (i < text.size()) {
    // ---- Inline code: `...` ----
    if (text[i] == '`') {
      flush();
      size_t end = text.find('`', i + 1);
      if (end != std::string_view::npos) {
        nodes.emplace_back(
            InlineCode{std::string(text.substr(i + 1, end - i - 1))});
        i = end + 1;
      } else {
        plain += text[i];
        ++i;
      }
      continue;
    }

    // ---- Bold: **...** ----
    if (i + 1 < text.size() && text[i] == '*' && text[i + 1] == '*') {
      flush();
      size_t end = text.find("**", i + 2);
      if (end != std::string_view::npos) {
        nodes.emplace_back(Bold{std::string(text.substr(i + 2, end - i - 2))});
        i = end + 2;
      } else {
        plain += text[i];
        ++i;
      }
      continue;
    }

    // ---- Italic: *...* (single asterisk, not preceded/followed by *) ----
    if (text[i] == '*' && (i + 1 >= text.size() || text[i + 1] != '*')) {
      flush();
      size_t end = text.find('*', i + 1);
      if (end != std::string_view::npos &&
          (end + 1 >= text.size() || text[end + 1] != '*')) {
        nodes.emplace_back(
            Italic{std::string(text.substr(i + 1, end - i - 1))});
        i = end + 1;
      } else {
        plain += text[i];
        ++i;
      }
      continue;
    }

    // ---- Strikethrough: ~~...~~ ----
    if (i + 1 < text.size() && text[i] == '~' && text[i + 1] == '~') {
      flush();
      size_t end = text.find("~~", i + 2);
      if (end != std::string_view::npos) {
        nodes.emplace_back(
            Strikethrough{std::string(text.substr(i + 2, end - i - 2))});
        i = end + 2;
      } else {
        plain += text[i];
        ++i;
      }
      continue;
    }

    // ---- Link: [text](url) ----
    if (text[i] == '[') {
      size_t bracket_end = text.find(']', i + 1);
      if (bracket_end != std::string_view::npos &&
          bracket_end + 1 < text.size() && text[bracket_end + 1] == '(') {
        size_t paren_end = text.find(')', bracket_end + 2);
        if (paren_end != std::string_view::npos) {
          flush();
          auto link_text = text.substr(i + 1, bracket_end - i - 1);
          auto url = text.substr(bracket_end + 2, paren_end - bracket_end - 2);
          nodes.emplace_back(Link{std::string(link_text), std::string(url)});
          i = paren_end + 1;
          continue;
        }
      }
    }

    // ---- Default: accumulate plain character ----
    plain += text[i];
    ++i;
  }

  flush();
  return nodes;
}

}  // namespace yac::presentation::markdown
