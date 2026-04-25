#include "lexer.hpp"

#include "../../theme.hpp"

#include <cctype>
#include <string>
#include <utility>

namespace yac::presentation::syntax::internal {

namespace {

bool IsIdentStart(char c) {
  return (std::isalpha(static_cast<unsigned char>(c)) != 0) || c == '_';
}

bool IsIdentChar(char c) {
  return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
}

bool IsHexDigit(char c) {
  return std::isxdigit(static_cast<unsigned char>(c)) != 0;
}

bool MatchAt(std::string_view s, size_t pos, std::string_view needle) {
  if (needle.empty() || pos + needle.size() > s.size()) {
    return false;
  }
  return s.compare(pos, needle.size(), needle) == 0;
}

void EmitPlain(std::vector<TokenSpan>& spans, std::string& buf) {
  if (buf.empty()) {
    return;
  }
  if (!spans.empty() && spans.back().kind == TokenKind::Plain) {
    spans.back().text += buf;
  } else {
    spans.push_back({TokenKind::Plain, buf});
  }
  buf.clear();
}

size_t ScanNumber(std::string_view line, size_t pos, const LanguageDef& lang) {
  if (pos >= line.size() ||
      std::isdigit(static_cast<unsigned char>(line[pos])) == 0) {
    return 0;
  }

  size_t i = pos;

  if (lang.number_hex_bin_oct && line[i] == '0' && i + 1 < line.size()) {
    char p = line[i + 1];
    if (p == 'x' || p == 'X' || p == 'b' || p == 'B' || p == 'o' || p == 'O') {
      i += 2;
      auto is_body_digit = [p](char ch) {
        if (p == 'x' || p == 'X') {
          return IsHexDigit(ch);
        }
        if (p == 'b' || p == 'B') {
          return ch == '0' || ch == '1';
        }
        return ch >= '0' && ch <= '7';
      };
      while (i < line.size()) {
        char ch = line[i];
        if (is_body_digit(ch) || (lang.number_underscores && ch == '_')) {
          ++i;
        } else {
          break;
        }
      }
      while (i < line.size() &&
             std::isalnum(static_cast<unsigned char>(line[i])) != 0) {
        ++i;
      }
      return i - pos;
    }
  }

  while (i < line.size() &&
         (std::isdigit(static_cast<unsigned char>(line[i])) != 0 ||
          (lang.number_underscores && line[i] == '_'))) {
    ++i;
  }
  if (i + 1 < line.size() && line[i] == '.' &&
      std::isdigit(static_cast<unsigned char>(line[i + 1])) != 0) {
    ++i;
    while (i < line.size() &&
           (std::isdigit(static_cast<unsigned char>(line[i])) != 0 ||
            (lang.number_underscores && line[i] == '_'))) {
      ++i;
    }
  }
  if (i < line.size() && (line[i] == 'e' || line[i] == 'E')) {
    size_t saved = i;
    ++i;
    if (i < line.size() && (line[i] == '+' || line[i] == '-')) {
      ++i;
    }
    bool seen = false;
    while (i < line.size() &&
           std::isdigit(static_cast<unsigned char>(line[i])) != 0) {
      ++i;
      seen = true;
    }
    if (!seen) {
      i = saved;
    }
  }
  while (i < line.size() &&
         (std::isalpha(static_cast<unsigned char>(line[i])) != 0 ||
          line[i] == '_')) {
    ++i;
  }
  return i - pos;
}

size_t FindStringEnd(std::string_view line, size_t pos, std::string_view closer,
                     bool allow_escapes) {
  size_t i = pos;
  while (i < line.size()) {
    if (allow_escapes && line[i] == '\\' && i + 1 < line.size()) {
      i += 2;
      continue;
    }
    if (MatchAt(line, i, closer)) {
      return i;
    }
    ++i;
  }
  return std::string_view::npos;
}

}  // namespace

Lexer::Lexer(const LanguageDef& lang) : lang_(&lang) {}

std::vector<TokenSpan> Lexer::NextLine(std::string_view line) {
  if (lang_->name == "diff") {
    return NextLineDiff(line);
  }
  return NextLineDefault(line);
}

std::vector<TokenSpan> Lexer::NextLineDiff(std::string_view line) {
  if (line.starts_with("+++ ") || line.starts_with("--- ") ||
      line.starts_with("diff --git") || line.starts_with("index ")) {
    return {{TokenKind::Preprocessor, std::string(line)}};
  }
  if (line.starts_with("@@")) {
    return {{TokenKind::Type, std::string(line)}};
  }
  if (!line.empty() && line.front() == '+') {
    return {{TokenKind::Number, std::string(line)}};
  }
  if (!line.empty() && line.front() == '-') {
    return {{TokenKind::Keyword, std::string(line)}};
  }
  return {{TokenKind::Plain, std::string(line)}};
}

std::vector<TokenSpan> Lexer::NextLineDefault(std::string_view line) {
  std::vector<TokenSpan> spans;
  std::string buf;
  size_t i = 0;

  if (state_ == State::BlockComment) {
    const auto& close = lang_->multi_line_comment_close;
    if (close.empty()) {
      state_ = State::Default;
    } else {
      auto idx = line.find(close);
      if (idx == std::string_view::npos) {
        spans.push_back({TokenKind::Comment, std::string(line)});
        return spans;
      }
      spans.push_back({TokenKind::Comment,
                       std::string(line.substr(0, idx + close.size()))});
      i = idx + close.size();
      state_ = State::Default;
    }
  }

  if (state_ == State::MultilineString && active_string_rule_ != nullptr) {
    auto idx = FindStringEnd(line, i, active_string_rule_->closer,
                             active_string_rule_->allow_escapes);
    if (idx == std::string_view::npos) {
      spans.push_back({TokenKind::String, std::string(line.substr(i))});
      return spans;
    }
    auto end = idx + active_string_rule_->closer.size();
    spans.push_back({TokenKind::String, std::string(line.substr(i, end - i))});
    i = end;
    state_ = State::Default;
    active_string_rule_ = nullptr;
  }

  if (state_ == State::Default && !lang_->preprocessor_prefix.empty() &&
      i == 0) {
    size_t lead = 0;
    while (lead < line.size() && (line[lead] == ' ' || line[lead] == '\t')) {
      ++lead;
    }
    bool collides = !lang_->single_line_comment.empty() &&
                    lang_->single_line_comment == lang_->preprocessor_prefix;
    if (!collides && MatchAt(line, lead, lang_->preprocessor_prefix)) {
      return {{TokenKind::Preprocessor, std::string(line)}};
    }
  }

  while (i < line.size()) {
    char c = line[i];

    if (!lang_->single_line_comment.empty() &&
        MatchAt(line, i, lang_->single_line_comment)) {
      EmitPlain(spans, buf);
      spans.push_back({TokenKind::Comment, std::string(line.substr(i))});
      return spans;
    }

    if (!lang_->multi_line_comment_open.empty() &&
        MatchAt(line, i, lang_->multi_line_comment_open)) {
      EmitPlain(spans, buf);
      const auto& close = lang_->multi_line_comment_close;
      auto search_from = i + lang_->multi_line_comment_open.size();
      auto end_idx = close.empty() ? std::string_view::npos
                                   : line.find(close, search_from);
      if (end_idx == std::string_view::npos) {
        spans.push_back({TokenKind::Comment, std::string(line.substr(i))});
        state_ = State::BlockComment;
        return spans;
      }
      auto stop = end_idx + close.size();
      spans.push_back(
          {TokenKind::Comment, std::string(line.substr(i, stop - i))});
      i = stop;
      continue;
    }

    bool matched_string = false;
    for (const auto& rule : lang_->string_rules) {
      if (!MatchAt(line, i, rule.opener)) {
        continue;
      }
      EmitPlain(spans, buf);
      auto search_from = i + rule.opener.size();
      auto end_idx =
          FindStringEnd(line, search_from, rule.closer, rule.allow_escapes);
      if (end_idx == std::string_view::npos) {
        if (rule.multiline) {
          spans.push_back({TokenKind::String, std::string(line.substr(i))});
          state_ = State::MultilineString;
          active_string_rule_ = &rule;
          return spans;
        }
        spans.push_back({TokenKind::String, std::string(line.substr(i))});
        return spans;
      }
      auto stop = end_idx + rule.closer.size();
      spans.push_back(
          {TokenKind::String, std::string(line.substr(i, stop - i))});
      i = stop;
      matched_string = true;
      break;
    }
    if (matched_string) {
      continue;
    }

    if (std::isdigit(static_cast<unsigned char>(c)) != 0) {
      auto len = ScanNumber(line, i, *lang_);
      if (len > 0) {
        EmitPlain(spans, buf);
        spans.push_back({TokenKind::Number, std::string(line.substr(i, len))});
        i += len;
        continue;
      }
    }

    if (lang_->decorator_prefix != 0 && c == lang_->decorator_prefix &&
        i + 1 < line.size() && IsIdentStart(line[i + 1])) {
      EmitPlain(spans, buf);
      size_t j = i + 1;
      while (j < line.size() && IsIdentChar(line[j])) {
        ++j;
      }
      spans.push_back(
          {TokenKind::Decorator, std::string(line.substr(i, j - i))});
      i = j;
      continue;
    }

    if (lang_->variable_dollar && c == '$' && i + 1 < line.size()) {
      if (line[i + 1] == '{') {
        EmitPlain(spans, buf);
        auto close = line.find('}', i + 2);
        size_t end = close == std::string_view::npos ? line.size() : close + 1;
        spans.push_back(
            {TokenKind::Variable, std::string(line.substr(i, end - i))});
        i = end;
        continue;
      }
      if (IsIdentStart(line[i + 1])) {
        EmitPlain(spans, buf);
        size_t j = i + 1;
        while (j < line.size() && IsIdentChar(line[j])) {
          ++j;
        }
        spans.push_back(
            {TokenKind::Variable, std::string(line.substr(i, j - i))});
        i = j;
        continue;
      }
    }

    if (IsIdentStart(c)) {
      size_t j = i + 1;
      while (j < line.size() && IsIdentChar(line[j])) {
        ++j;
      }
      auto word = std::string(line.substr(i, j - i));
      TokenKind kind = TokenKind::Plain;
      if (lang_->keywords.contains(word)) {
        kind = TokenKind::Keyword;
      } else if (lang_->types.contains(word)) {
        kind = TokenKind::Type;
      } else {
        size_t k = j;
        while (k < line.size() && (line[k] == ' ' || line[k] == '\t')) {
          ++k;
        }
        if (k < line.size() && line[k] == '(') {
          kind = TokenKind::FunctionCall;
        }
      }
      if (kind == TokenKind::Plain) {
        buf += word;
      } else {
        EmitPlain(spans, buf);
        spans.push_back({kind, std::move(word)});
      }
      i = j;
      continue;
    }

    buf += c;
    ++i;
  }

  EmitPlain(spans, buf);
  return spans;
}

ftxui::Element RenderToken(const TokenSpan& span,
                           const RenderContext& context) {
  const auto& theme = context.Colors();
  auto element = ftxui::text(span.text);
  switch (span.kind) {
    case TokenKind::Keyword:
      return element | ftxui::color(theme.syntax.keyword) | ftxui::bold;
    case TokenKind::Type:
      return element | ftxui::color(theme.syntax.type);
    case TokenKind::Number:
      return element | ftxui::color(theme.syntax.number);
    case TokenKind::String:
      return element | ftxui::color(theme.syntax.string);
    case TokenKind::Comment:
      return element | ftxui::color(theme.syntax.comment);
    case TokenKind::FunctionCall:
      return element | ftxui::color(theme.syntax.function);
    case TokenKind::Preprocessor:
      return element | ftxui::color(theme.syntax.keyword);
    case TokenKind::Decorator:
    case TokenKind::Variable:
      return element | ftxui::color(theme.syntax.type);
    case TokenKind::Plain:
      return element;
  }
  return element;
}

}  // namespace yac::presentation::syntax::internal
