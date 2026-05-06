#include "inline_tokenizer.hpp"

#include <cctype>
#include <string>
#include <utility>

namespace yac::presentation::markdown::parser_detail {

namespace {

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

  bool TryEscape() {
    if (pos_ + 1 >= text_.size()) {
      return false;
    }
    const char next = text_[pos_ + 1];
    if (!IsEscapable(next)) {
      return false;
    }
    buf_ += next;
    pos_ += 2;
    return true;
  }

  bool TryCodeSpan() {
    const size_t run_start = pos_;
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
      const size_t close_start = scan;
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
    const size_t end = text_.find('>', pos_ + 1);
    if (end == std::string_view::npos) {
      return false;
    }

    std::string body(text_.substr(pos_ + 1, end - pos_ - 1));
    if (body.empty()) {
      return false;
    }
    const auto colon = body.find(':');
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
    nodes_.emplace_back(Link{.text = body, .url = body});
    pos_ = end + 1;
    return true;
  }

  bool TryImage() {
    if (pos_ + 1 >= text_.size() || text_[pos_ + 1] != '[') {
      return false;
    }
    const size_t bracket_start = pos_ + 1;
    const auto bracket_end = ScanBalanced(bracket_start, '[', ']');
    if (bracket_end == std::string_view::npos) {
      return false;
    }
    if (bracket_end + 1 >= text_.size() || text_[bracket_end + 1] != '(') {
      return false;
    }
    const size_t paren_start = bracket_end + 1;
    const auto paren_end = ScanBalanced(paren_start, '(', ')');
    if (paren_end == std::string_view::npos) {
      return false;
    }
    std::string alt(
        text_.substr(bracket_start + 1, bracket_end - bracket_start - 1));
    std::string url(text_.substr(paren_start + 1, paren_end - paren_start - 1));
    FlushText();
    nodes_.emplace_back(Image{.alt = std::move(alt), .url = std::move(url)});
    pos_ = paren_end + 1;
    return true;
  }

  bool TryLink() {
    const auto bracket_end = ScanBalanced(pos_, '[', ']');
    if (bracket_end == std::string_view::npos) {
      return false;
    }
    if (bracket_end + 1 >= text_.size() || text_[bracket_end + 1] != '(') {
      return false;
    }
    const size_t paren_start = bracket_end + 1;
    const auto paren_end = ScanBalanced(paren_start, '(', ')');
    if (paren_end == std::string_view::npos) {
      return false;
    }
    std::string label(text_.substr(pos_ + 1, bracket_end - pos_ - 1));
    std::string url(text_.substr(paren_start + 1, paren_end - paren_start - 1));
    FlushText();
    nodes_.emplace_back(Link{.text = std::move(label), .url = std::move(url)});
    pos_ = paren_end + 1;
    return true;
  }

  [[nodiscard]] size_t ScanBalanced(size_t start, char open, char close) const {
    if (start >= text_.size() || text_[start] != open) {
      return std::string_view::npos;
    }
    int depth = 0;
    for (size_t i = start; i < text_.size(); ++i) {
      const char c = text_[i];
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

  [[nodiscard]] bool LeftFlanking(size_t run_start,
                                  size_t run_end_exclusive) const {
    const char before = run_start == 0 ? ' ' : text_[run_start - 1];
    const char after =
        run_end_exclusive < text_.size() ? text_[run_end_exclusive] : ' ';
    const bool followed_by_ws =
        std::isspace(static_cast<unsigned char>(after)) != 0;
    if (followed_by_ws) {
      return false;
    }
    const bool followed_by_punct = IsAsciiPunct(after);
    const bool preceded_by_ws =
        std::isspace(static_cast<unsigned char>(before)) != 0;
    const bool preceded_by_punct = IsAsciiPunct(before);
    if (!followed_by_punct) {
      return true;
    }
    return preceded_by_ws || preceded_by_punct;
  }

  [[nodiscard]] bool RightFlanking(size_t run_start,
                                   size_t run_end_exclusive) const {
    const char before = run_start == 0 ? ' ' : text_[run_start - 1];
    const char after =
        run_end_exclusive < text_.size() ? text_[run_end_exclusive] : ' ';
    const bool preceded_by_ws =
        std::isspace(static_cast<unsigned char>(before)) != 0;
    if (preceded_by_ws) {
      return false;
    }
    const bool preceded_by_punct = IsAsciiPunct(before);
    const bool followed_by_ws =
        std::isspace(static_cast<unsigned char>(after)) != 0;
    const bool followed_by_punct = IsAsciiPunct(after);
    if (!preceded_by_punct) {
      return true;
    }
    return followed_by_ws || followed_by_punct;
  }

  [[nodiscard]] bool CanOpen(char delim, size_t run_start,
                             size_t run_end_exclusive) const {
    if (!LeftFlanking(run_start, run_end_exclusive)) {
      return false;
    }
    if (delim == '_' && RightFlanking(run_start, run_end_exclusive)) {
      const char before = run_start == 0 ? ' ' : text_[run_start - 1];
      return IsAsciiPunct(before);
    }
    return true;
  }

  [[nodiscard]] bool CanClose(char delim, size_t run_start,
                              size_t run_end_exclusive) const {
    if (!RightFlanking(run_start, run_end_exclusive)) {
      return false;
    }
    if (delim == '_' && LeftFlanking(run_start, run_end_exclusive)) {
      const char after =
          run_end_exclusive < text_.size() ? text_[run_end_exclusive] : ' ';
      return IsAsciiPunct(after);
    }
    return true;
  }

  // Skips past an inline code span starting at `scan` (`text_[scan]` must be
  // a backtick). Returns the position after the matching close run, or
  // `scan + 1` when no matching close exists — preserving the prior
  // single-byte advance behavior so emphasis scanning makes forward progress.
  [[nodiscard]] size_t SkipCodeSpan(size_t scan) const {
    size_t code_run = 0;
    while (scan + code_run < text_.size() && text_[scan + code_run] == '`') {
      ++code_run;
    }
    size_t close_scan = scan + code_run;
    while (close_scan < text_.size()) {
      if (text_[close_scan] != '`') {
        ++close_scan;
        continue;
      }
      size_t close_run = 0;
      while (close_scan + close_run < text_.size() &&
             text_[close_scan + close_run] == '`') {
        ++close_run;
      }
      if (close_run == code_run) {
        return close_scan + close_run;
      }
      close_scan += close_run;
    }
    return scan + 1;
  }

  bool TryEmphasis(char delim) {
    size_t run_len = 0;
    while (pos_ + run_len < text_.size() && text_[pos_ + run_len] == delim) {
      ++run_len;
    }
    const size_t run_end = pos_ + run_len;
    if (!CanOpen(delim, pos_, run_end)) {
      return false;
    }
    const size_t want = run_len >= 2 ? 2 : 1;
    size_t scan = run_end;
    while (scan < text_.size()) {
      const char c = text_[scan];
      if (c == '\\' && scan + 1 < text_.size()) {
        scan += 2;
        continue;
      }
      if (c == '`') {
        scan = SkipCodeSpan(scan);
        continue;
      }
      if (c != delim) {
        ++scan;
        continue;
      }
      const size_t close_start = scan;
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
      const size_t content_start = run_end;
      const size_t content_end = close_start;
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
    const size_t run_end = pos_ + 2;
    if (!LeftFlanking(pos_, run_end)) {
      return false;
    }
    size_t scan = run_end;
    while (scan + 1 < text_.size()) {
      const char c = text_[scan];
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
      const char prev = text_[pos_ - 1];
      if (std::isalnum(static_cast<unsigned char>(prev)) != 0 || prev == '/' ||
          prev == ':') {
        return false;
      }
    }

    const std::string_view rest = text_.substr(pos_);
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
      const char last = text_[end - 1];
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
    nodes_.emplace_back(
        Link{.text = std::move(display), .url = std::move(url)});
    pos_ = end;
    return true;
  }

  std::string_view text_;
  size_t pos_ = 0;
  std::vector<InlineNode> nodes_;
  std::string buf_;
};

}  // namespace

std::vector<InlineNode> ParseInlineNodes(std::string_view text) {
  return InlineTokenizer(text).Tokenize();
}

}  // namespace yac::presentation::markdown::parser_detail
