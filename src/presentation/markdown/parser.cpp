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

bool LineLooksLikeBlock(const std::string& line) {
  if (line.starts_with("```") || line.starts_with("~~~")) {
    return true;
  }
  auto trimmed = TrimLeft(line);
  if (trimmed.empty()) {
    return true;
  }
  if (trimmed.starts_with("#") || trimmed.starts_with(">") ||
      trimmed.starts_with("- ") || trimmed.starts_with("* ") ||
      trimmed.starts_with("+ ")) {
    return true;
  }
  if (std::isdigit(static_cast<unsigned char>(trimmed[0])) != 0) {
    size_t idx = 0;
    while (idx < trimmed.size() &&
           std::isdigit(static_cast<unsigned char>(trimmed[idx])) != 0) {
      ++idx;
    }
    if (idx < trimmed.size() && trimmed[idx] == '.') {
      return true;
    }
  }
  return false;
}

bool HasUnescapedPipe(std::string_view s) {
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size() && s[i + 1] == '|') {
      ++i;
      continue;
    }
    if (s[i] == '|') {
      return true;
    }
  }
  return false;
}

std::vector<std::string> SplitTableRow(std::string_view line) {
  auto trimmed = Trim(line);
  std::string_view view = trimmed;
  if (!view.empty() && view.front() == '|') {
    view.remove_prefix(1);
  }
  if (!view.empty() && view.back() == '|' &&
      (view.size() < 2 || view[view.size() - 2] != '\\')) {
    view.remove_suffix(1);
  }

  std::vector<std::string> cells;
  std::string current;
  for (size_t i = 0; i < view.size(); ++i) {
    if (view[i] == '\\' && i + 1 < view.size() && view[i + 1] == '|') {
      current += '|';
      ++i;
      continue;
    }
    if (view[i] == '|') {
      cells.push_back(Trim(current));
      current.clear();
      continue;
    }
    current += view[i];
  }
  cells.push_back(Trim(current));
  return cells;
}

std::optional<std::vector<ColumnAlignment>> ParseDelimiterRow(
    std::string_view line) {
  auto trimmed = Trim(line);
  if (trimmed.empty()) {
    return std::nullopt;
  }

  std::string_view view = trimmed;
  if (!view.empty() && view.front() == '|') {
    view.remove_prefix(1);
  }
  if (!view.empty() && view.back() == '|') {
    view.remove_suffix(1);
  }

  std::vector<ColumnAlignment> alignments;
  std::string current;
  auto flush = [&](const std::string& cell) -> bool {
    auto c = Trim(cell);
    if (c.empty()) {
      return false;
    }
    bool left_colon = c.front() == ':';
    bool right_colon = c.back() == ':';
    size_t dash_start = left_colon ? 1 : 0;
    size_t dash_end = right_colon ? c.size() - 1 : c.size();
    if (dash_start >= dash_end) {
      return false;
    }
    for (size_t i = dash_start; i < dash_end; ++i) {
      if (c[i] != '-') {
        return false;
      }
    }
    if (left_colon && right_colon) {
      alignments.push_back(ColumnAlignment::Center);
    } else if (left_colon) {
      alignments.push_back(ColumnAlignment::Left);
    } else if (right_colon) {
      alignments.push_back(ColumnAlignment::Right);
    } else {
      alignments.push_back(ColumnAlignment::Default);
    }
    return true;
  };

  for (char ch : view) {
    if (ch == '|') {
      if (!flush(current)) {
        return std::nullopt;
      }
      current.clear();
    } else {
      current += ch;
    }
  }
  if (!flush(current)) {
    return std::nullopt;
  }
  if (alignments.empty()) {
    return std::nullopt;
  }
  return alignments;
}

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

    if (auto tbl = TryParseTable(lines, i)) {
      blocks.emplace_back(std::move(*tbl));
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

std::optional<Table> MarkdownParser::TryParseTable(
    const std::vector<std::string>& lines, size_t& index) {
  if (index + 1 >= lines.size()) {
    return std::nullopt;
  }

  const auto& header_line = lines[index];
  if (Trim(header_line).empty()) {
    return std::nullopt;
  }
  if (!HasUnescapedPipe(header_line)) {
    return std::nullopt;
  }

  auto alignments = ParseDelimiterRow(lines[index + 1]);
  if (!alignments) {
    return std::nullopt;
  }

  auto header_cells = SplitTableRow(header_line);
  if (header_cells.size() != alignments->size()) {
    return std::nullopt;
  }

  Table table;
  table.columns = std::move(*alignments);
  table.header.reserve(table.columns.size());
  for (auto& cell : header_cells) {
    table.header.push_back(ParseInline(cell));
  }

  size_t cursor = index + 2;
  while (cursor < lines.size()) {
    const auto& row_line = lines[cursor];
    if (Trim(row_line).empty()) {
      break;
    }
    if (LineLooksLikeBlock(row_line)) {
      break;
    }
    auto raw_cells = SplitTableRow(row_line);
    Table::Row row;
    row.reserve(table.columns.size());
    for (size_t c = 0; c < table.columns.size(); ++c) {
      if (c < raw_cells.size()) {
        row.push_back(ParseInline(raw_cells[c]));
      } else {
        row.emplace_back();
      }
    }
    table.rows.push_back(std::move(row));
    ++cursor;
  }

  index = cursor;
  return table;
}

std::vector<InlineNode> MarkdownParser::ParseInline(std::string_view text) {
  std::vector<InlineNode> nodes;

  static const std::regex
      kInlineFormatPattern(  // NOLINT(readability-identifier-naming)
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
