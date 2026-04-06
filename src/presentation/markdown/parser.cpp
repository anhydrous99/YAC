#include "parser.hpp"

#include <algorithm>
#include <cctype>
#include <regex>

namespace yac::presentation::markdown {

namespace {

std::vector<std::string> SplitLines(std::string_view text) {
  std::vector<std::string> lines;
  std::string line;
  for (char c : text) {
    if (c == '\n') {
      lines.push_back(std::move(line));
      line.clear();
    } else {
      line += c;
    }
  }
  if (!line.empty()) {
    lines.push_back(std::move(line));
  }
  return lines;
}

std::string Trim(std::string_view s) {
  auto start = s.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) {
    return "";
  }
  auto end = s.find_last_not_of(" \t\r\n");
  return std::string(s.substr(start, end - start + 1));
}

std::string TrimLeft(std::string_view s) {
  auto start = s.find_first_not_of(" \t");
  if (start == std::string_view::npos) {
    return "";
  }
  return std::string(s.substr(start));
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

    if (auto bq = TryParseBlockquote(line)) {
      blocks.emplace_back(std::move(*bq));
      ++i;
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

    Paragraph para;
    std::string para_text;
    while (i < lines.size() && !Trim(lines[i]).empty()) {
      if (lines[i].starts_with("#") || lines[i].starts_with(">") ||
          lines[i].starts_with("- ") || lines[i].starts_with("* ") ||
          lines[i].starts_with("`") ||
          std::isdigit(static_cast<unsigned char>(lines[i][0])) != 0) {
        bool is_list =
            std::isdigit(static_cast<unsigned char>(lines[i][0])) != 0;
        if (is_list) {
          size_t idx = 0;
          while (idx < lines[i].size() &&
                 std::isdigit(static_cast<unsigned char>(lines[i][idx])) != 0) {
            ++idx;
          }
          is_list = (idx < lines[i].size() && lines[i][idx] == '.');
        }
        if (!is_list && lines[i].starts_with("`")) {
          break;
        }
        if (is_list || lines[i].starts_with("#") || lines[i].starts_with(">") ||
            lines[i].starts_with("- ") || lines[i].starts_with("* ")) {
          break;
        }
      }
      if (!para_text.empty()) {
        para_text += ' ';
      }
      para_text += Trim(lines[i]);
      ++i;
    }
    para.children = ParseInline(para_text);
    blocks.emplace_back(std::move(para));
  }

  return blocks;
}

std::optional<Heading> MarkdownParser::TryParseHeading(
    const std::string& line) {
  auto trimmed = TrimLeft(line);
  int level = 0;
  while (level < static_cast<int>(trimmed.size()) && trimmed[level] == '#') {
    ++level;
  }
  if (level == 0 || level > 6) {
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
  if (!trimmed.starts_with("```")) {
    return std::nullopt;
  }

  CodeBlock cb;
  cb.language = Trim(trimmed.substr(3));
  ++index;

  std::string source;
  while (index < lines.size()) {
    if (TrimLeft(lines[index]).starts_with("```")) {
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
    const std::string& line) {
  auto trimmed = TrimLeft(line);
  if (!trimmed.starts_with(">")) {
    return std::nullopt;
  }

  Blockquote bq;
  bq.children = ParseInline(Trim(trimmed.substr(1)));
  return bq;
}

std::optional<UnorderedList::Item> MarkdownParser::TryParseUnorderedItem(
    const std::string& line) {
  auto trimmed = TrimLeft(line);
  if (!trimmed.starts_with("- ") && !trimmed.starts_with("* ")) {
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

std::vector<InlineNode> MarkdownParser::ParseInline(std::string_view text) {
  std::vector<InlineNode> nodes;

  static const std::regex k_pattern(
      R"((`\S.*?\S?`)|(\*\*.+?\*\*)|(\*.+?\*)|(~~.+?~~)|(\[.+?\]\(.+?\)))");

  std::string remaining(text);
  std::smatch match;

  while (std::regex_search(remaining, match, k_pattern)) {
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
