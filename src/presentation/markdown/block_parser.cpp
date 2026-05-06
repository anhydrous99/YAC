#include "block_parser.hpp"

#include "inline_tokenizer.hpp"
#include "presentation/util/string_util.hpp"

#include <algorithm>
#include <cctype>
#include <iterator>
#include <optional>
#include <string_view>
#include <utility>

namespace yac::presentation::markdown::parser_detail {

namespace {

using util::Trim;
using util::TrimLeft;

constexpr int kMaxHeadingLevel = 6;

bool LineLooksLikeBlock(const std::string& line) {
  const auto trimmed = TrimLeft(line);
  if (trimmed.empty()) {
    return true;
  }
  if (trimmed.starts_with("```") || trimmed.starts_with("~~~")) {
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

size_t LeadingSpaces(const std::string& line) {
  size_t i = 0;
  while (i < line.size() && line[i] == ' ') {
    ++i;
  }
  return i;
}

struct ListItemHeader {
  bool ordered{false};
  int order_value{1};
  size_t marker_col{0};
  size_t content_col{0};
  bool task{false};
  bool task_checked{false};
  std::string first_line_content;
};

// Recognizes the GFM task-list `[ ]` / `[x]` / `[X]` prefix at the start of a
// list item's content. Returns nullopt when the prefix is absent.
struct TaskMarker {
  bool checked = false;
  size_t consumed = 0;  // Bytes from `text` belonging to the marker.
};

std::optional<TaskMarker> ParseTaskMarker(std::string_view text) {
  if (text.size() < 3 || text[0] != '[' ||
      (text[1] != ' ' && text[1] != 'x' && text[1] != 'X') || text[2] != ']') {
    return std::nullopt;
  }
  if (text.size() != 3 && text[3] != ' ') {
    return std::nullopt;
  }
  return TaskMarker{
      .checked = (text[1] == 'x' || text[1] == 'X'),
      .consumed = text.size() <= 4 ? text.size() : 4,
  };
}

std::optional<ListItemHeader> ParseListItemHeader(const std::string& line) {
  const size_t indent = LeadingSpaces(line);
  if (indent >= line.size()) {
    return std::nullopt;
  }
  ListItemHeader header;
  header.marker_col = indent;
  size_t pos = indent;
  const char c = line[pos];
  size_t marker_len = 0;
  if (c == '-' || c == '*' || c == '+') {
    marker_len = 1;
  } else if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
    size_t end = pos;
    while (end < line.size() &&
           std::isdigit(static_cast<unsigned char>(line[end])) != 0) {
      ++end;
    }
    if (end >= line.size() || line[end] != '.') {
      return std::nullopt;
    }
    header.ordered = true;
    header.order_value = std::stoi(line.substr(pos, end - pos));
    marker_len = end - pos + 1;
  } else {
    return std::nullopt;
  }

  const size_t after_marker = pos + marker_len;
  if (after_marker == line.size()) {
    header.content_col = after_marker + 1;
    return header;
  }
  if (line[after_marker] != ' ' && line[after_marker] != '\t') {
    return std::nullopt;
  }
  size_t content_start = after_marker + 1;
  while (content_start < line.size() &&
         (line[content_start] == ' ' || line[content_start] == '\t')) {
    ++content_start;
  }
  header.content_col = after_marker + 1;
  std::string remainder = line.substr(content_start);
  if (!header.ordered) {
    if (auto task = ParseTaskMarker(remainder); task.has_value()) {
      header.task = true;
      header.task_checked = task->checked;
      header.first_line_content = task->consumed < remainder.size()
                                      ? remainder.substr(task->consumed)
                                      : std::string{};
      return header;
    }
  }
  header.first_line_content = std::move(remainder);
  return header;
}

std::string DedentLine(const std::string& line, size_t n) {
  size_t strip = 0;
  while (strip < n && strip < line.size() && line[strip] == ' ') {
    ++strip;
  }
  return line.substr(strip);
}

bool HasUnescapedPipe(std::string_view value) {
  for (size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size() && value[i + 1] == '|') {
      ++i;
      continue;
    }
    if (value[i] == '|') {
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
    const bool left_colon = c.front() == ':';
    const bool right_colon = c.back() == ':';
    const size_t dash_start = left_colon ? 1 : 0;
    const size_t dash_end = right_colon ? c.size() - 1 : c.size();
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

size_t TryParseParagraph(const std::vector<std::string>& lines, size_t start,
                         std::vector<BlockNode>& blocks) {
  auto is_setext_underline = [](const std::string& line, char marker) {
    const auto trimmed = Trim(line);
    return !trimmed.empty() && std::ranges::all_of(trimmed, [marker](char c) {
      return c == marker;
    });
  };

  Paragraph paragraph;
  size_t index = start;
  bool first_line = true;
  bool pending_hard_break = false;
  while (index < lines.size() && !Trim(lines[index]).empty()) {
    if (!first_line && LineLooksLikeBlock(lines[index])) {
      break;
    }
    if (!first_line && (is_setext_underline(lines[index], '=') ||
                        is_setext_underline(lines[index], '-'))) {
      Heading heading;
      heading.level = is_setext_underline(lines[index], '=') ? 1 : 2;
      heading.children = std::move(paragraph.children);
      blocks.emplace_back(std::move(heading));
      return index + 1;
    }
    if (!first_line) {
      paragraph.children.emplace_back(
          pending_hard_break ? InlineNode{LineBreak{}} : InlineNode{Text{" "}});
    }

    const auto& raw = lines[index];
    const bool ends_with_two_spaces = raw.size() >= 2 &&
                                      raw[raw.size() - 1] == ' ' &&
                                      raw[raw.size() - 2] == ' ';
    std::string content = Trim(raw);
    const bool ends_with_backslash = !content.empty() && content.back() == '\\';
    if (ends_with_backslash) {
      content.pop_back();
    }
    pending_hard_break = ends_with_two_spaces || ends_with_backslash;

    auto inline_nodes = ParseInlineNodes(content);
    for (auto& node : inline_nodes) {
      paragraph.children.push_back(std::move(node));
    }
    first_line = false;
    ++index;
  }
  if (index == start) {
    ++index;
  }
  blocks.emplace_back(std::move(paragraph));
  return index;
}

std::optional<Heading> TryParseHeading(const std::string& line) {
  const auto trimmed = TrimLeft(line);
  int level = 0;
  while (std::cmp_less(level, trimmed.size()) && trimmed[level] == '#') {
    ++level;
  }
  if (level == 0 || level > kMaxHeadingLevel) {
    return std::nullopt;
  }
  if (std::cmp_less(level, trimmed.size()) &&
      std::isspace(static_cast<unsigned char>(trimmed[level])) == 0) {
    return std::nullopt;
  }

  Heading heading;
  heading.level = level;
  heading.children = ParseInlineNodes(Trim(trimmed.substr(level)));
  return heading;
}

std::optional<CodeBlock> TryParseCodeBlock(
    const std::vector<std::string>& lines, size_t& index,
    const ParseOptions& opts) {
  if (index >= lines.size()) {
    return std::nullopt;
  }

  const auto trimmed = TrimLeft(lines[index]);
  std::string fence;
  if (trimmed.starts_with("```")) {
    fence = "```";
  } else if (trimmed.starts_with("~~~")) {
    fence = "~~~";
  } else {
    return std::nullopt;
  }

  CodeBlock code_block;
  code_block.language = Trim(trimmed.substr(fence.size()));
  ++index;

  std::string source;
  bool closed = false;
  while (index < lines.size()) {
    if (TrimLeft(lines[index]).starts_with(fence)) {
      ++index;
      closed = true;
      break;
    }
    if (!source.empty()) {
      source += '\n';
    }
    source += lines[index];
    ++index;
  }

  code_block.source = source;
  code_block.partial = opts.streaming && !closed;
  return code_block;
}

std::optional<Blockquote> TryParseBlockquote(
    const std::vector<std::string>& lines, size_t& index,
    const ParseOptions& opts) {
  const auto trimmed = TrimLeft(lines[index]);
  if (!trimmed.starts_with(">")) {
    return std::nullopt;
  }

  std::vector<std::string> stripped;
  while (index < lines.size()) {
    const auto t = TrimLeft(lines[index]);
    if (!t.starts_with(">")) {
      break;
    }
    size_t pos = 1;
    if (pos < t.size() && t[pos] == ' ') {
      ++pos;
    }
    stripped.push_back(t.substr(pos));
    ++index;
  }

  Blockquote blockquote;
  blockquote.children = ParseBlockNodes(stripped, opts);
  return blockquote;
}

// Skips a blank-line run from `index` and, if the next non-blank line is a
// list item header at the same marker column with matching ordering, advances
// `index` to that line. Returns true when the list should continue, false
// when the run terminates the list (EOF or different marker shape).
bool SkipBlankRunUntilNextItem(const std::vector<std::string>& lines,
                               size_t& index, size_t marker_col, bool ordered) {
  size_t look = index + 1;
  while (look < lines.size() && Trim(lines[look]).empty()) {
    ++look;
  }
  if (look >= lines.size()) {
    index = look;
    return false;
  }
  auto peek = ParseListItemHeader(lines[look]);
  if (!peek || peek->marker_col != marker_col || peek->ordered != ordered) {
    return false;
  }
  index = look;
  return true;
}

// Reads the continuation lines of a single list item starting at `index` and
// returns them dedented to `content_col`. Stops when a non-indented line or
// an inter-item blank run is reached. `index` is advanced to the next line
// the outer loop should consider.
std::vector<std::string> CollectListItemBody(
    const std::vector<std::string>& lines, size_t& index, size_t content_col) {
  std::vector<std::string> body;
  while (index < lines.size()) {
    const auto& line = lines[index];
    if (Trim(line).empty()) {
      size_t look = index + 1;
      while (look < lines.size() && Trim(lines[look]).empty()) {
        ++look;
      }
      if (look >= lines.size() || LeadingSpaces(lines[look]) < content_col) {
        break;
      }
      body.emplace_back();
      ++index;
      continue;
    }
    if (LeadingSpaces(line) < content_col) {
      break;
    }
    body.push_back(DedentLine(line, content_col));
    ++index;
  }
  while (!body.empty() && Trim(body.back()).empty()) {
    body.pop_back();
  }
  return body;
}

std::optional<BlockNode> TryParseList(const std::vector<std::string>& lines,
                                      size_t& index, const ParseOptions& opts) {
  if (index >= lines.size()) {
    return std::nullopt;
  }
  auto first = ParseListItemHeader(lines[index]);
  if (!first.has_value()) {
    return std::nullopt;
  }

  const bool ordered = first->ordered;
  const size_t marker_col = first->marker_col;

  UnorderedList unordered_list;
  OrderedList ordered_list;
  if (ordered) {
    ordered_list.start = first->order_value;
  }

  while (index < lines.size()) {
    if (Trim(lines[index]).empty()) {
      if (!SkipBlankRunUntilNextItem(lines, index, marker_col, ordered)) {
        break;
      }
      continue;
    }

    auto header = ParseListItemHeader(lines[index]);
    if (!header || header->marker_col != marker_col ||
        header->ordered != ordered) {
      break;
    }
    const auto header_value = *header;

    std::vector<std::string> body;
    body.push_back(header_value.first_line_content);
    ++index;
    auto continuation =
        CollectListItemBody(lines, index, header_value.content_col);
    body.insert(body.end(), std::make_move_iterator(continuation.begin()),
                std::make_move_iterator(continuation.end()));

    auto children = ParseBlockNodes(body, opts);
    if (ordered) {
      OrderedList::Item item;
      item.children = std::move(children);
      ordered_list.items.push_back(std::move(item));
    } else {
      UnorderedList::Item item;
      item.children = std::move(children);
      item.task = header_value.task;
      item.checked = header_value.task_checked;
      unordered_list.items.push_back(std::move(item));
    }
  }

  if (ordered) {
    if (ordered_list.items.empty()) {
      return std::nullopt;
    }
    return MakeBlock(std::move(ordered_list));
  }
  if (unordered_list.items.empty()) {
    return std::nullopt;
  }
  return MakeBlock(std::move(unordered_list));
}

std::optional<HorizontalRule> TryParseHorizontalRule(const std::string& line) {
  const auto trimmed = Trim(line);
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

std::optional<Table> TryParseTable(const std::vector<std::string>& lines,
                                   size_t& index) {
  if (index + 1 >= lines.size()) {
    return std::nullopt;
  }

  const auto& header_line = lines[index];
  if (Trim(header_line).empty() || !HasUnescapedPipe(header_line)) {
    return std::nullopt;
  }

  auto alignments = ParseDelimiterRow(lines[index + 1]);
  if (!alignments.has_value()) {
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
    table.header.push_back(ParseInlineNodes(cell));
  }

  size_t cursor = index + 2;
  while (cursor < lines.size()) {
    const auto& row_line = lines[cursor];
    if (Trim(row_line).empty() || LineLooksLikeBlock(row_line)) {
      break;
    }
    auto raw_cells = SplitTableRow(row_line);
    Table::Row row;
    row.reserve(table.columns.size());
    for (size_t column = 0; column < table.columns.size(); ++column) {
      if (column < raw_cells.size()) {
        row.push_back(ParseInlineNodes(raw_cells[column]));
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

}  // namespace

std::vector<BlockNode> ParseBlockNodes(const std::vector<std::string>& lines,
                                       const ParseOptions& opts) {
  std::vector<BlockNode> blocks;
  size_t index = 0;

  while (index < lines.size()) {
    if (auto code_block = TryParseCodeBlock(lines, index, opts)) {
      blocks.emplace_back(std::move(*code_block));
      continue;
    }

    const auto& line = lines[index];
    if (Trim(line).empty()) {
      ++index;
      continue;
    }
    if (auto heading = TryParseHeading(line)) {
      blocks.emplace_back(std::move(*heading));
      ++index;
      continue;
    }
    if (auto rule = TryParseHorizontalRule(line)) {
      blocks.emplace_back(std::move(*rule));
      ++index;
      continue;
    }
    if (auto blockquote = TryParseBlockquote(lines, index, opts)) {
      blocks.emplace_back(MakeBlock(std::move(*blockquote)));
      continue;
    }
    if (auto list_block = TryParseList(lines, index, opts)) {
      blocks.emplace_back(std::move(*list_block));
      continue;
    }
    if (auto table = TryParseTable(lines, index)) {
      blocks.emplace_back(std::move(*table));
      continue;
    }

    const bool last_was_paragraph =
        !blocks.empty() && std::holds_alternative<Paragraph>(blocks.back());
    if (!last_was_paragraph && LeadingSpaces(line) >= 4) {
      CodeBlock code_block;
      std::string source;
      while (index < lines.size()) {
        if (Trim(lines[index]).empty()) {
          size_t look = index + 1;
          while (look < lines.size() && Trim(lines[look]).empty()) {
            ++look;
          }
          if (look >= lines.size() || LeadingSpaces(lines[look]) < 4) {
            break;
          }
          if (!source.empty()) {
            source += '\n';
          }
          ++index;
          continue;
        }
        if (LeadingSpaces(lines[index]) < 4) {
          break;
        }
        if (!source.empty()) {
          source += '\n';
        }
        source += lines[index].substr(4);
        ++index;
      }
      code_block.source = std::move(source);
      blocks.emplace_back(std::move(code_block));
      continue;
    }

    index = TryParseParagraph(lines, index, blocks);
  }

  return blocks;
}

}  // namespace yac::presentation::markdown::parser_detail
