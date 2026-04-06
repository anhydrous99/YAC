#include "parser.hpp"

#include "presentation/util/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace yac::presentation::markdown {

namespace {

using util::SplitLines;
using util::Trim;
using util::TrimLeft;

}  // namespace

std::vector<BlockNode> MarkdownParser::Parse(std::string_view markdown) {
  auto lines = SplitLines(markdown);
  return ParseBlocks(lines);
}

std::vector<BlockNode> MarkdownParser::ParseBlocks(
    const std::vector<std::string>& lines) {
  std::vector<BlockNode> blocks;
  size_t i = 0;

  while (i < lines.size()) {
    if (auto cb = TryParseCodeBlock(lines, i)) {
      blocks.emplace_back(std::move(*cb));
      continue;
    }

    const auto& line = lines[i];

    if (Trim(line).empty()) {
      ++i;
      continue;
    }

    if (auto h = TryParseHeading(line)) {
      blocks.emplace_back(std::move(*h));
      ++i;
      continue;
    }

    if (auto hr = TryParseHorizontalRule(line)) {
      blocks.emplace_back(std::move(*hr));
      ++i;
      continue;
    }

    if (auto bq = TryParseBlockquote(lines, i)) {
      blocks.emplace_back(std::move(*bq));
      continue;
    }

    if (auto item = TryParseUnorderedItem(line)) {
      UnorderedList ul;
      while (i < lines.size()) {
        if (auto next_item = TryParseUnorderedItem(lines[i])) {
          ul.items.push_back(std::move(*next_item));
          ++i;
        } else if (Trim(lines[i]).empty()) {
          ++i;
          break;
        } else {
          break;
        }
      }
      blocks.emplace_back(std::move(ul));
      continue;
    }

    if (auto item = TryParseOrderedItem(line)) {
      OrderedList ol;
      while (i < lines.size()) {
        if (auto next_item = TryParseOrderedItem(lines[i])) {
          ol.items.push_back(std::move(*next_item));
          ++i;
        } else if (Trim(lines[i]).empty()) {
          ++i;
          break;
        } else {
          break;
        }
      }
      blocks.emplace_back(std::move(ol));
      continue;
    }

    i = TryParseParagraph(lines, i, blocks);
  }

  return blocks;
}

size_t MarkdownParser::TryParseParagraph(const std::vector<std::string>& lines,
                                         size_t start,
                                         std::vector<BlockNode>& blocks) {
  Paragraph para;
  size_t i = start;
  bool first_line = true;
  while (i < lines.size() && !Trim(lines[i]).empty()) {
    if (lines[i].starts_with("#") || lines[i].starts_with(">") ||
        lines[i].starts_with("- ") || lines[i].starts_with("* ") ||
        lines[i].starts_with("+ ") ||
        std::isdigit(static_cast<unsigned char>(lines[i][0])) != 0) {
      bool is_list = std::isdigit(static_cast<unsigned char>(lines[i][0])) != 0;
      if (is_list) {
        size_t idx = 0;
        while (idx < lines[i].size() &&
               std::isdigit(static_cast<unsigned char>(lines[i][idx])) != 0) {
          ++idx;
        }
        is_list = (idx < lines[i].size() && lines[i][idx] == '.');
      }
      if (is_list || lines[i].starts_with("#") || lines[i].starts_with(">") ||
          lines[i].starts_with("- ") || lines[i].starts_with("* ") ||
          lines[i].starts_with("+ ")) {
        break;
      }
    }
    if (!first_line) {
      para.children.emplace_back(Text{" "});
    }
    first_line = false;
    auto inline_nodes = ParseInline(Trim(lines[i]));
    for (auto& node : inline_nodes) {
      para.children.push_back(std::move(node));
    }
    ++i;
  }
  if (i == start) {
    ++i;
  }
  blocks.emplace_back(std::move(para));
  return i;
}

std::optional<Heading> MarkdownParser::TryParseHeading(
    const std::string& line) {
  auto trimmed = TrimLeft(line);
  int level = 0;
  while (level < static_cast<int>(trimmed.size()) && trimmed[level] == '#') {
    ++level;
  }
  if (level == 0 || level > kMaxHeadingLevel) {
    return std::nullopt;
  }
  if (level < static_cast<int>(trimmed.size()) &&
      std::isspace(static_cast<unsigned char>(trimmed[level])) == 0) {
    return std::nullopt;
  }

  Heading h;
  h.level = level;
  h.children = ParseInline(Trim(trimmed.substr(level)));
  return h;
}

std::optional<CodeBlock> MarkdownParser::TryParseCodeBlock(
    const std::vector<std::string>& lines, size_t& index) {
  if (index >= lines.size()) {
    return std::nullopt;
  }

  auto trimmed = TrimLeft(lines[index]);
  std::string fence;
  if (trimmed.starts_with("```")) {
    fence = "```";
  } else if (trimmed.starts_with("~~~")) {
    fence = "~~~";
  } else {
    return std::nullopt;
  }

  CodeBlock cb;
  cb.language = Trim(trimmed.substr(fence.size()));
  ++index;

  std::string source;
  while (index < lines.size()) {
    if (TrimLeft(lines[index]).starts_with(fence)) {
      ++index;
      break;
    }
    if (!source.empty()) {
      source += '\n';
    }
    source += lines[index];
    ++index;
  }

  cb.source = source;
  return cb;
}

std::optional<Blockquote> MarkdownParser::TryParseBlockquote(
    const std::vector<std::string>& lines, size_t& index) {
  auto trimmed = TrimLeft(lines[index]);
  if (!trimmed.starts_with(">")) {
    return std::nullopt;
  }

  Blockquote bq;
  int nesting = 0;
  size_t pos = 1;
  while (pos < trimmed.size() && trimmed[pos] == '>') {
    ++nesting;
    ++pos;
  }
  if (nesting == 0) {
    nesting = 0;
  }
  if (pos < trimmed.size() && trimmed[pos] == ' ') {
    ++pos;
  }

  auto first_line = ParseInline(Trim(trimmed.substr(pos)));
  for (auto& node : first_line) {
    bq.lines.emplace_back();
    bq.lines.back().push_back(std::move(node));
  }
  if (bq.lines.empty()) {
    bq.lines.emplace_back();
  }
  bq.nesting_level = nesting;
  ++index;

  while (index < lines.size()) {
    auto next_trimmed = TrimLeft(lines[index]);
    if (!next_trimmed.starts_with(">")) {
      break;
    }
    size_t next_pos = 1;
    if (next_pos < next_trimmed.size() && next_trimmed[next_pos] == ' ') {
      ++next_pos;
    }
    auto next_inline = ParseInline(Trim(next_trimmed.substr(next_pos)));
    bq.lines.push_back(std::move(next_inline));
    ++index;
  }

  return bq;
}

std::optional<UnorderedList::Item> MarkdownParser::TryParseUnorderedItem(
    const std::string& line) {
  auto trimmed = TrimLeft(line);
  if (!trimmed.starts_with("- ") && !trimmed.starts_with("* ") &&
      !trimmed.starts_with("+ ")) {
    return std::nullopt;
  }

  UnorderedList::Item item;
  item.children = ParseInline(Trim(trimmed.substr(2)));
  return item;
}

std::optional<OrderedList::Item> MarkdownParser::TryParseOrderedItem(
    const std::string& line) {
  auto trimmed = TrimLeft(line);
  size_t idx = 0;
  while (idx < trimmed.size() &&
         std::isdigit(static_cast<unsigned char>(trimmed[idx])) != 0) {
    ++idx;
  }
  if (idx == 0 || idx >= trimmed.size() || trimmed[idx] != '.') {
    return std::nullopt;
  }

  OrderedList::Item item;
  item.children = ParseInline(Trim(trimmed.substr(idx + 1)));
  return item;
}

std::optional<HorizontalRule> MarkdownParser::TryParseHorizontalRule(
    const std::string& line) {
  auto trimmed = Trim(line);
  if (trimmed.size() < 3) {
    return std::nullopt;
  }

  const char marker = trimmed[0];
  if (marker != '-' && marker != '*' && marker != '_') {
    return std::nullopt;
  }

  for (char c : trimmed) {
    if (c != marker) {
      return std::nullopt;
    }
  }

  return HorizontalRule{};
}

std::vector<InlineNode> MarkdownParser::ParseInline(std::string_view text) {
  std::vector<InlineNode> nodes;

  static const std::regex kInlineFormatPattern(
      R"((`\S.*?\S?`)|(\*\*.+?\*\*)|(\*.+?\*)|(~~.+?~~)|(\[.+?\]\(.+?\)))");

  std::string remaining(text);
  std::smatch match;

  while (std::regex_search(remaining, match, kInlineFormatPattern)) {
    if (match.prefix().length() > 0) {
      nodes.emplace_back(Text{match.prefix().str()});
    }

    std::string matched = match.str();
    if (matched.starts_with("`") && matched.ends_with("`")) {
      nodes.emplace_back(InlineCode{matched.substr(1, matched.length() - 2)});
    } else if (matched.starts_with("**") && matched.ends_with("**")) {
      nodes.emplace_back(Bold{matched.substr(2, matched.length() - 4)});
    } else if (matched.starts_with("*") && matched.ends_with("*")) {
      nodes.emplace_back(Italic{matched.substr(1, matched.length() - 2)});
    } else if (matched.starts_with("~~") && matched.ends_with("~~")) {
      nodes.emplace_back(
          Strikethrough{matched.substr(2, matched.length() - 4)});
    } else if (matched.starts_with("[")) {
      auto paren_open = matched.find("](");
      if (paren_open != std::string::npos) {
        auto text_part = matched.substr(1, paren_open - 1);
        auto url_part =
            matched.substr(paren_open + 2, matched.length() - paren_open - 3);
        nodes.emplace_back(Link{text_part, url_part});
      }
    }

    remaining = match.suffix();
  }

  if (!remaining.empty()) {
    nodes.emplace_back(Text{remaining});
  }

  return nodes;
}

}  // namespace yac::presentation::markdown
