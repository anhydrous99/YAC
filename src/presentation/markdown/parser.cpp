#include "parser.hpp"

#include "presentation/util/string_util.hpp"

#include <algorithm>
#include <cctype>

namespace yac::presentation::markdown {

namespace {

using util::SplitLines;
using util::Trim;
using util::TrimLeft;

bool IsAsciiPunct(char c) {
  return (c >= '!' && c <= '/') || (c >= ':' && c <= '@') ||
         (c >= '[' && c <= '`') || (c >= '{' && c <= '~');
}

bool IsEscapable(char c) {
  switch (c) {
    case '\\':
    case '`':
    case '*':
    case '_':
    case '{':
    case '}':
    case '[':
    case ']':
    case '(':
    case ')':
    case '#':
    case '+':
    case '-':
    case '.':
    case '!':
    case '~':
    case '<':
    case '>':
    case '|':
    case '"':
    case '\'':
    case ':':
    case '/':
    case '=':
    case '&':
    case '$':
    case ';':
    case ',':
    case '?':
    case '@':
    case '^':
      return true;
    default:
      return false;
  }
}

bool IsUrlChar(char c) {
  if (std::isalnum(static_cast<unsigned char>(c)) != 0) {
    return true;
  }
  switch (c) {
    case '-':
    case '.':
    case '_':
    case '~':
    case ':':
    case '/':
    case '?':
    case '#':
    case '[':
    case ']':
    case '@':
    case '!':
    case '$':
    case '&':
    case '\'':
    case '*':
    case '+':
    case ',':
    case ';':
    case '=':
    case '%':
    case '(':
    case ')':
      return true;
    default:
      return false;
  }
}

class InlineTokenizer {
 public:
  explicit InlineTokenizer(std::string_view text) : text_(text) {}

  std::vector<InlineNode> Tokenize() {
    while (pos_ < text_.size()) {
      char c = text_[pos_];
      if (c == '\\' && TryEscape()) {
        continue;
      }
      if (c == '`' && TryCodeSpan()) {
        continue;
      }
      if (c == '<' && TryAutolink()) {
        continue;
      }
      if (c == '!' && TryImage()) {
        continue;
      }
      if (c == '[' && TryLink()) {
        continue;
      }
      if ((c == '*' || c == '_') && TryEmphasis(c)) {
        continue;
      }
      if (c == '~' && TryStrikethrough()) {
        continue;
      }
      if ((c == 'h' || c == 'H' || c == 'w' || c == 'W') && TryBareUrl()) {
        continue;
      }
      buf_ += c;
      ++pos_;
    }
    FlushText();
    return std::move(nodes_);
  }

 private:
  void FlushText() {
    if (!buf_.empty()) {
      nodes_.emplace_back(Text{std::move(buf_)});
      buf_.clear();
    }
  }

  char PrevChar() const { return pos_ == 0 ? ' ' : text_[pos_ - 1]; }

  char CharAt(size_t i) const { return i < text_.size() ? text_[i] : ' '; }

  bool TryEscape() {
    if (pos_ + 1 >= text_.size()) {
      return false;
    }
    char next = text_[pos_ + 1];
    if (!IsEscapable(next)) {
      return false;
    }
    buf_ += next;
    pos_ += 2;
    return true;
  }

  bool TryCodeSpan() {
    size_t run_start = pos_;
    size_t run_len = 0;
    while (pos_ + run_len < text_.size() && text_[pos_ + run_len] == '`') {
      ++run_len;
    }
    size_t scan = pos_ + run_len;
    while (scan < text_.size()) {
      if (text_[scan] != '`') {
        ++scan;
        continue;
      }
      size_t close_start = scan;
      size_t close_len = 0;
      while (scan < text_.size() && text_[scan] == '`') {
        ++close_len;
        ++scan;
      }
      if (close_len == run_len) {
        std::string content(text_.substr(run_start + run_len,
                                         close_start - (run_start + run_len)));
        if (content.size() >= 2 && content.front() == ' ' &&
            content.back() == ' ' &&
            content.find_first_not_of(' ') != std::string::npos) {
          content = content.substr(1, content.size() - 2);
        }
        FlushText();
        nodes_.emplace_back(InlineCode{std::move(content)});
        pos_ = scan;
        return true;
      }
    }
    return false;
  }

  bool TryAutolink() {
    size_t end = text_.find('>', pos_ + 1);
    if (end == std::string_view::npos) {
      return false;
    }
    std::string body(text_.substr(pos_ + 1, end - pos_ - 1));
    if (body.empty()) {
      return false;
    }
    auto colon = body.find(':');
    if (colon == std::string::npos || colon == 0) {
      if (body.find('@') == std::string::npos) {
        return false;
      }
    }
    for (char c : body) {
      if (c == ' ' || c == '<' || c == '>') {
        return false;
      }
    }
    FlushText();
    nodes_.emplace_back(Link{body, body});
    pos_ = end + 1;
    return true;
  }

  bool TryImage() {
    if (pos_ + 1 >= text_.size() || text_[pos_ + 1] != '[') {
      return false;
    }
    size_t bracket_start = pos_ + 1;
    auto bracket_end = ScanBalanced(bracket_start, '[', ']');
    if (bracket_end == std::string_view::npos) {
      return false;
    }
    if (bracket_end + 1 >= text_.size() || text_[bracket_end + 1] != '(') {
      return false;
    }
    size_t paren_start = bracket_end + 1;
    auto paren_end = ScanBalanced(paren_start, '(', ')');
    if (paren_end == std::string_view::npos) {
      return false;
    }
    std::string alt(
        text_.substr(bracket_start + 1, bracket_end - bracket_start - 1));
    std::string url(text_.substr(paren_start + 1, paren_end - paren_start - 1));
    FlushText();
    nodes_.emplace_back(Image{std::move(alt), std::move(url)});
    pos_ = paren_end + 1;
    return true;
  }

  bool TryLink() {
    auto bracket_end = ScanBalanced(pos_, '[', ']');
    if (bracket_end == std::string_view::npos) {
      return false;
    }
    if (bracket_end + 1 >= text_.size() || text_[bracket_end + 1] != '(') {
      return false;
    }
    size_t paren_start = bracket_end + 1;
    auto paren_end = ScanBalanced(paren_start, '(', ')');
    if (paren_end == std::string_view::npos) {
      return false;
    }
    std::string label(text_.substr(pos_ + 1, bracket_end - pos_ - 1));
    std::string url(text_.substr(paren_start + 1, paren_end - paren_start - 1));
    FlushText();
    nodes_.emplace_back(Link{std::move(label), std::move(url)});
    pos_ = paren_end + 1;
    return true;
  }

  size_t ScanBalanced(size_t start, char open, char close) const {
    if (start >= text_.size() || text_[start] != open) {
      return std::string_view::npos;
    }
    int depth = 0;
    for (size_t i = start; i < text_.size(); ++i) {
      char c = text_[i];
      if (c == '\\' && i + 1 < text_.size()) {
        ++i;
        continue;
      }
      if (c == open) {
        ++depth;
      } else if (c == close) {
        --depth;
        if (depth == 0) {
          return i;
        }
      }
    }
    return std::string_view::npos;
  }

  bool LeftFlanking(size_t run_start, size_t run_end_exclusive) const {
    char before = run_start == 0 ? ' ' : text_[run_start - 1];
    char after =
        run_end_exclusive < text_.size() ? text_[run_end_exclusive] : ' ';
    bool followed_by_ws = std::isspace(static_cast<unsigned char>(after)) != 0;
    if (followed_by_ws) {
      return false;
    }
    bool followed_by_punct = IsAsciiPunct(after);
    bool preceded_by_ws = std::isspace(static_cast<unsigned char>(before)) != 0;
    bool preceded_by_punct = IsAsciiPunct(before);
    if (!followed_by_punct) {
      return true;
    }
    return preceded_by_ws || preceded_by_punct;
  }

  bool RightFlanking(size_t run_start, size_t run_end_exclusive) const {
    char before = run_start == 0 ? ' ' : text_[run_start - 1];
    char after =
        run_end_exclusive < text_.size() ? text_[run_end_exclusive] : ' ';
    bool preceded_by_ws = std::isspace(static_cast<unsigned char>(before)) != 0;
    if (preceded_by_ws) {
      return false;
    }
    bool preceded_by_punct = IsAsciiPunct(before);
    bool followed_by_ws = std::isspace(static_cast<unsigned char>(after)) != 0;
    bool followed_by_punct = IsAsciiPunct(after);
    if (!preceded_by_punct) {
      return true;
    }
    return followed_by_ws || followed_by_punct;
  }

  bool CanOpen(char delim, size_t run_start, size_t run_end_exclusive) const {
    if (!LeftFlanking(run_start, run_end_exclusive)) {
      return false;
    }
    if (delim == '_' && RightFlanking(run_start, run_end_exclusive)) {
      char before = run_start == 0 ? ' ' : text_[run_start - 1];
      return IsAsciiPunct(before);
    }
    return true;
  }

  bool CanClose(char delim, size_t run_start, size_t run_end_exclusive) const {
    if (!RightFlanking(run_start, run_end_exclusive)) {
      return false;
    }
    if (delim == '_' && LeftFlanking(run_start, run_end_exclusive)) {
      char after =
          run_end_exclusive < text_.size() ? text_[run_end_exclusive] : ' ';
      return IsAsciiPunct(after);
    }
    return true;
  }

  bool TryEmphasis(char delim) {
    size_t run_len = 0;
    while (pos_ + run_len < text_.size() && text_[pos_ + run_len] == delim) {
      ++run_len;
    }
    size_t run_end = pos_ + run_len;
    if (!CanOpen(delim, pos_, run_end)) {
      return false;
    }
    size_t want = (run_len >= 2) ? 2 : 1;
    size_t scan = run_end;
    while (scan < text_.size()) {
      char c = text_[scan];
      if (c == '\\' && scan + 1 < text_.size()) {
        scan += 2;
        continue;
      }
      if (c == '`') {
        size_t code_run = 0;
        while (scan + code_run < text_.size() &&
               text_[scan + code_run] == '`') {
          ++code_run;
        }
        size_t close_scan = scan + code_run;
        bool found_close = false;
        while (close_scan < text_.size()) {
          if (text_[close_scan] != '`') {
            ++close_scan;
            continue;
          }
          size_t cr = 0;
          while (close_scan + cr < text_.size() &&
                 text_[close_scan + cr] == '`') {
            ++cr;
          }
          if (cr == code_run) {
            close_scan += cr;
            scan = close_scan;
            found_close = true;
            break;
          }
          close_scan += cr;
        }
        if (!found_close) {
          ++scan;
        }
        continue;
      }
      if (c != delim) {
        ++scan;
        continue;
      }
      size_t close_start = scan;
      size_t close_len = 0;
      while (scan < text_.size() && text_[scan] == delim) {
        ++close_len;
        ++scan;
      }
      if (close_len < want) {
        continue;
      }
      if (!CanClose(delim, close_start, close_start + close_len)) {
        continue;
      }
      size_t content_start = run_end;
      size_t content_end = close_start;
      std::string content(
          text_.substr(content_start, content_end - content_start));
      FlushText();
      if (want == 2) {
        nodes_.emplace_back(Bold{std::move(content)});
        pos_ = close_start + 2;
      } else {
        nodes_.emplace_back(Italic{std::move(content)});
        pos_ = close_start + 1;
      }
      return true;
    }
    return false;
  }

  bool TryStrikethrough() {
    if (pos_ + 1 >= text_.size() || text_[pos_ + 1] != '~') {
      return false;
    }
    size_t run_end = pos_ + 2;
    if (!LeftFlanking(pos_, run_end)) {
      return false;
    }
    size_t scan = run_end;
    while (scan + 1 < text_.size()) {
      char c = text_[scan];
      if (c == '\\' && scan + 1 < text_.size()) {
        scan += 2;
        continue;
      }
      if (c == '~' && text_[scan + 1] == '~') {
        if (!RightFlanking(scan, scan + 2)) {
          ++scan;
          continue;
        }
        std::string content(text_.substr(run_end, scan - run_end));
        FlushText();
        nodes_.emplace_back(Strikethrough{std::move(content)});
        pos_ = scan + 2;
        return true;
      }
      ++scan;
    }
    return false;
  }

  bool TryBareUrl() {
    if (pos_ != 0) {
      char prev = text_[pos_ - 1];
      if (std::isalnum(static_cast<unsigned char>(prev)) != 0 || prev == '/' ||
          prev == ':') {
        return false;
      }
    }
    std::string_view rest = text_.substr(pos_);
    size_t prefix_len = 0;
    if (rest.starts_with("https://") || rest.starts_with("HTTPS://")) {
      prefix_len = 8;
    } else if (rest.starts_with("http://") || rest.starts_with("HTTP://")) {
      prefix_len = 7;
    } else if (rest.starts_with("www.") || rest.starts_with("WWW.")) {
      prefix_len = 4;
    } else {
      return false;
    }
    size_t end = pos_ + prefix_len;
    while (end < text_.size() && IsUrlChar(text_[end])) {
      ++end;
    }
    while (end > pos_ + prefix_len) {
      char last = text_[end - 1];
      if (last == '.' || last == ',' || last == ';' || last == ':' ||
          last == ')' || last == ']' || last == '!' || last == '?') {
        --end;
        continue;
      }
      break;
    }
    if (end - pos_ <= prefix_len) {
      return false;
    }
    std::string url(text_.substr(pos_, end - pos_));
    std::string display = url;
    FlushText();
    nodes_.emplace_back(Link{std::move(display), std::move(url)});
    pos_ = end;
    return true;
  }

  std::string_view text_;
  size_t pos_ = 0;
  std::vector<InlineNode> nodes_;
  std::string buf_;
};

bool LineLooksLikeBlock(const std::string& line) {
  auto trimmed = TrimLeft(line);
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

std::optional<ListItemHeader> ParseListItemHeader(const std::string& line) {
  size_t indent = LeadingSpaces(line);
  if (indent >= line.size()) {
    return std::nullopt;
  }
  ListItemHeader h;
  h.marker_col = indent;
  size_t pos = indent;
  char c = line[pos];
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
    h.ordered = true;
    h.order_value = std::stoi(line.substr(pos, end - pos));
    marker_len = end - pos + 1;
  } else {
    return std::nullopt;
  }
  size_t after_marker = pos + marker_len;
  if (after_marker == line.size()) {
    h.content_col = after_marker + 1;
    return h;
  }
  if (line[after_marker] != ' ' && line[after_marker] != '\t') {
    return std::nullopt;
  }
  size_t content_start = after_marker + 1;
  while (content_start < line.size() &&
         (line[content_start] == ' ' || line[content_start] == '\t')) {
    ++content_start;
  }
  h.content_col = after_marker + 1;
  std::string remainder = line.substr(content_start);
  if (!h.ordered && remainder.size() >= 3 && remainder[0] == '[' &&
      (remainder[1] == ' ' || remainder[1] == 'x' || remainder[1] == 'X') &&
      remainder[2] == ']' && (remainder.size() == 3 || remainder[3] == ' ')) {
    h.task = true;
    h.task_checked = (remainder[1] == 'x' || remainder[1] == 'X');
    if (remainder.size() <= 4) {
      h.first_line_content.clear();
    } else {
      h.first_line_content = remainder.substr(4);
    }
  } else {
    h.first_line_content = std::move(remainder);
  }
  return h;
}

std::string DedentLine(const std::string& line, size_t n) {
  size_t strip = 0;
  while (strip < n && strip < line.size() && line[strip] == ' ') {
    ++strip;
  }
  return line.substr(strip);
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
  return Parse(markdown, ParseOptions{});
}

std::vector<BlockNode> MarkdownParser::Parse(std::string_view markdown,
                                             const ParseOptions& opts) {
  auto lines = SplitLines(markdown);
  return ParseBlocks(lines, opts);
}

std::vector<BlockNode> MarkdownParser::ParseBlocks(
    const std::vector<std::string>& lines, const ParseOptions& opts) {
  std::vector<BlockNode> blocks;
  size_t i = 0;

  while (i < lines.size()) {
    if (auto cb = TryParseCodeBlock(lines, i, opts)) {
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

    if (auto bq = TryParseBlockquote(lines, i, opts)) {
      blocks.emplace_back(MakeBlock(std::move(*bq)));
      continue;
    }

    if (auto list_block = TryParseList(lines, i, opts)) {
      blocks.emplace_back(std::move(*list_block));
      continue;
    }

    if (auto tbl = TryParseTable(lines, i)) {
      blocks.emplace_back(std::move(*tbl));
      continue;
    }

    // Indented code block: 4+ leading spaces, only when not interrupting a
    // paragraph (the paragraph fallback below would otherwise be hit first).
    const bool last_was_paragraph =
        !blocks.empty() && std::holds_alternative<Paragraph>(blocks.back());
    if (!last_was_paragraph && LeadingSpaces(line) >= 4) {
      CodeBlock cb;
      std::string source;
      while (i < lines.size()) {
        if (Trim(lines[i]).empty()) {
          // Blank lines belong to the code block only if a non-blank indented
          // line follows.
          size_t look = i + 1;
          while (look < lines.size() && Trim(lines[look]).empty()) {
            ++look;
          }
          if (look >= lines.size() || LeadingSpaces(lines[look]) < 4) {
            break;
          }
          if (!source.empty()) {
            source += '\n';
          }
          ++i;
          continue;
        }
        if (LeadingSpaces(lines[i]) < 4) {
          break;
        }
        if (!source.empty()) {
          source += '\n';
        }
        source += lines[i].substr(4);
        ++i;
      }
      cb.source = source;
      blocks.emplace_back(std::move(cb));
      continue;
    }

    i = TryParseParagraph(lines, i, blocks);
  }

  return blocks;
}

size_t MarkdownParser::TryParseParagraph(const std::vector<std::string>& lines,
                                         size_t start,
                                         std::vector<BlockNode>& blocks) {
  auto IsSetextUnderline = [](const std::string& line, char marker) {
    auto t = Trim(line);
    if (t.size() < 1) {
      return false;
    }
    for (char c : t) {
      if (c != marker) {
        return false;
      }
    }
    return true;
  };

  Paragraph para;
  size_t i = start;
  bool first_line = true;
  bool pending_hard_break = false;
  while (i < lines.size() && !Trim(lines[i]).empty()) {
    if (!first_line && LineLooksLikeBlock(lines[i])) {
      break;
    }
    // Setext heading: a non-first line of `=` (h1) or `-` (h2) terminates and
    // upgrades the paragraph.
    if (!first_line && (IsSetextUnderline(lines[i], '=') ||
                        IsSetextUnderline(lines[i], '-'))) {
      int level = IsSetextUnderline(lines[i], '=') ? 1 : 2;
      Heading h;
      h.level = level;
      h.children = std::move(para.children);
      blocks.emplace_back(std::move(h));
      return i + 1;
    }
    if (!first_line) {
      if (pending_hard_break) {
        para.children.emplace_back(LineBreak{});
      } else {
        para.children.emplace_back(Text{" "});
      }
    }

    const auto& raw = lines[i];
    bool ends_with_two_spaces = raw.size() >= 2 && raw[raw.size() - 1] == ' ' &&
                                raw[raw.size() - 2] == ' ';
    std::string content = Trim(raw);
    bool ends_with_backslash = !content.empty() && content.back() == '\\';
    if (ends_with_backslash) {
      content.pop_back();
    }
    pending_hard_break = ends_with_two_spaces || ends_with_backslash;

    auto inline_nodes = ParseInline(content);
    for (auto& node : inline_nodes) {
      para.children.push_back(std::move(node));
    }
    first_line = false;
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
    const std::vector<std::string>& lines, size_t& index,
    const ParseOptions& opts) {
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

  cb.source = source;
  cb.partial = opts.streaming && !closed;
  return cb;
}

std::optional<Blockquote> MarkdownParser::TryParseBlockquote(
    const std::vector<std::string>& lines, size_t& index,
    const ParseOptions& opts) {
  auto trimmed = TrimLeft(lines[index]);
  if (!trimmed.starts_with(">")) {
    return std::nullopt;
  }

  std::vector<std::string> stripped;
  while (index < lines.size()) {
    auto t = TrimLeft(lines[index]);
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

  Blockquote bq;
  bq.children = ParseBlocks(stripped, opts);
  return bq;
}

std::optional<BlockNode> MarkdownParser::TryParseList(
    const std::vector<std::string>& lines, size_t& index,
    const ParseOptions& opts) {
  if (index >= lines.size()) {
    return std::nullopt;
  }
  auto first = ParseListItemHeader(lines[index]);
  if (!first) {
    return std::nullopt;
  }

  bool ordered = first->ordered;
  size_t marker_col = first->marker_col;

  UnorderedList ul;
  OrderedList ol;
  if (ordered) {
    ol.start = first->order_value;
  }

  while (index < lines.size()) {
    if (Trim(lines[index]).empty()) {
      // Blank line: peek ahead. If next non-blank is another item at same
      // marker_col matching ordered/unordered, continue; otherwise stop.
      size_t look = index + 1;
      while (look < lines.size() && Trim(lines[look]).empty()) {
        ++look;
      }
      if (look >= lines.size()) {
        index = look;
        break;
      }
      auto peek = ParseListItemHeader(lines[look]);
      if (!peek || peek->marker_col != marker_col || peek->ordered != ordered) {
        break;
      }
      index = look;
      continue;
    }
    auto header = ParseListItemHeader(lines[index]);
    if (!header || header->marker_col != marker_col ||
        header->ordered != ordered) {
      break;
    }

    std::vector<std::string> body;
    body.push_back(header->first_line_content);
    ++index;
    size_t content_col = header->content_col;

    while (index < lines.size()) {
      const auto& line = lines[index];
      if (Trim(line).empty()) {
        // Look ahead: is the next non-blank line still inside this item?
        size_t look = index + 1;
        while (look < lines.size() && Trim(lines[look]).empty()) {
          ++look;
        }
        if (look >= lines.size()) {
          break;
        }
        size_t next_indent = LeadingSpaces(lines[look]);
        if (next_indent < content_col) {
          break;
        }
        body.emplace_back();
        ++index;
        continue;
      }
      size_t indent = LeadingSpaces(line);
      if (indent < content_col) {
        break;
      }
      body.push_back(DedentLine(line, content_col));
      ++index;
    }
    while (!body.empty() && Trim(body.back()).empty()) {
      body.pop_back();
    }

    auto children = ParseBlocks(body, opts);
    if (ordered) {
      OrderedList::Item item;
      item.children = std::move(children);
      ol.items.push_back(std::move(item));
    } else {
      UnorderedList::Item item;
      item.children = std::move(children);
      item.task = header->task;
      item.checked = header->task_checked;
      ul.items.push_back(std::move(item));
    }
  }

  if (ordered) {
    if (ol.items.empty()) {
      return std::nullopt;
    }
    return MakeBlock(std::move(ol));
  }
  if (ul.items.empty()) {
    return std::nullopt;
  }
  return MakeBlock(std::move(ul));
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
  return InlineTokenizer(text).Tokenize();
}

}  // namespace yac::presentation::markdown
