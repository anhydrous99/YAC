#include "highlighter.hpp"

#include "../theme.hpp"
#include "presentation/util/string_util.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <unordered_set>

namespace yac::presentation::syntax {

namespace {

using util::SplitLines;

inline const auto& k_theme = theme::Theme::Instance();

struct LanguageDef {
  std::string name;
  std::unordered_set<std::string> keywords;
  std::unordered_set<std::string> types;
  std::string single_line_comment;
  std::string multi_line_comment_open;
  std::string multi_line_comment_close;
};

std::string ToLower(std::string_view s) {
  std::string result(s);
  std::transform(result.begin(), result.end(), result.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return result;
}

const std::array<LanguageDef, 4> kLanguageDefinitions = {{
    {"cpp",
     {"auto",        "break",     "case",     "catch",     "class",
      "const",       "constexpr", "continue", "default",   "delete",
      "do",          "else",      "enum",     "explicit",  "extern",
      "false",       "final",     "for",      "friend",    "goto",
      "if",          "inline",    "mutable",  "namespace", "new",
      "noexcept",    "nullptr",   "operator", "override",  "private",
      "protected",   "public",    "register", "return",    "static",
      "static_cast", "struct",    "switch",   "template",  "this",
      "throw",       "true",      "try",      "typedef",   "typeid",
      "typename",    "union",     "using",    "virtual",   "volatile",
      "while"},
     {"int", "void", "bool", "char", "double", "float", "long", "short",
      "unsigned", "signed", "size_t", "string", "vector", "map", "set",
      "unique_ptr", "shared_ptr", "optional"},
     "//",
     "/*",
     "*/"},
    {"python",
     {"and",      "as",       "assert", "async", "await",  "break",  "class",
      "continue", "def",      "del",    "elif",  "else",   "except", "finally",
      "for",      "from",     "global", "if",    "import", "in",     "is",
      "lambda",   "nonlocal", "not",    "or",    "pass",   "raise",  "return",
      "try",      "while",    "with",   "yield", "true",   "false",  "none"},
     {"int", "str", "float", "bool", "list", "dict", "tuple", "set", "bytes",
      "object", "type", "none"},
     "#",
     "",
     ""},
    {"javascript",
     {"async",     "await",    "break",    "case",    "catch",      "class",
      "const",     "continue", "debugger", "default", "delete",     "do",
      "else",      "export",   "extends",  "false",   "finally",    "for",
      "function",  "if",       "import",   "in",      "instanceof", "let",
      "new",       "null",     "of",       "return",  "static",     "super",
      "switch",    "this",     "throw",    "true",    "try",        "typeof",
      "undefined", "var",      "void",     "while",   "with",       "yield"},
     {"number", "string", "boolean", "object", "array", "function", "undefined",
      "null", "symbol", "bigint", "map", "set", "promise"},
     "//",
     "/*",
     "*/"},
    {"rust",
     {"as",     "async", "await", "break",  "const",  "continue", "crate",
      "dyn",    "else",  "enum",  "extern", "false",  "fn",       "for",
      "if",     "impl",  "in",    "let",    "loop",   "match",    "mod",
      "move",   "mut",   "pub",   "ref",    "return", "self",     "static",
      "struct", "super", "trait", "true",   "type",   "unsafe",   "use",
      "where",  "while"},
     {"i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f32", "f64",
      "bool", "char", "str", "string", "vec", "option", "result", "box"},
     "//",
     "/*",
     "*/"},
}};

const LanguageDef* FindLanguage(std::string_view name) {
  auto lower = ToLower(name);
  for (const auto& lang : kLanguageDefinitions) {
    if (lang.name == lower) {
      return &lang;
    }
  }
  return nullptr;
}

bool IsKeyword(std::string_view word, const LanguageDef& lang) {
  auto lower = ToLower(word);
  return lang.keywords.contains(lower);
}

bool IsType(std::string_view word, const LanguageDef& lang) {
  auto lower = ToLower(word);
  return lang.keywords.contains(lower);
}

bool IsComment(std::string_view line, const LanguageDef& lang) {
  if (lang.single_line_comment.empty()) {
    return false;
  }
  return line.starts_with(lang.single_line_comment);
}

bool IsIdentifierChar(char c) {
  return (std::isalnum(static_cast<unsigned char>(c)) != 0) || c == '_';
}

bool IsNumber(std::string_view token) {
  if (token.empty()) {
    return false;
  }
  if ((std::isdigit(static_cast<unsigned char>(token[0])) == 0) &&
      token[0] != '.' && token[0] != '-') {
    return false;
  }
  bool has_dot = (token[0] == '.');
  for (size_t i = 1; i < token.size(); ++i) {
    char c = token[i];
    if (c == '.') {
      if (has_dot) {
        return false;
      }
      has_dot = true;
    } else if (std::isdigit(static_cast<unsigned char>(c)) == 0) {
      return false;
    }
  }
  return true;
}

void EmitToken(std::string& current, ftxui::Elements& segments,
               const LanguageDef& lang) {
  if (current.empty()) {
    return;
  }

  if (IsKeyword(current, lang)) {
    segments.push_back(ftxui::text(current) |
                       ftxui::color(k_theme.syntax.keyword) | ftxui::bold);
  } else if (IsType(current, lang)) {
    segments.push_back(ftxui::text(current) |
                       ftxui::color(k_theme.syntax.type));
  } else if (IsNumber(current)) {
    segments.push_back(ftxui::text(current) |
                       ftxui::color(k_theme.syntax.number));
  } else {
    segments.push_back(ftxui::text(current));
  }
  current.clear();
}

ftxui::Element HighlightLine(std::string_view line, const LanguageDef& lang) {
  using namespace ftxui;

  if (IsComment(line, lang)) {
    return text(std::string(line)) | color(k_theme.syntax.comment);
  }

  Elements segments;
  std::string current;
  bool in_string = false;
  char string_char = 0;

  for (size_t i = 0; i < line.size(); ++i) {
    char c = line[i];

    if (in_string) {
      current += c;
      if (c == string_char && (i == 0 || line[i - 1] != '\\')) {
        segments.push_back(text(current) | color(k_theme.syntax.string));
        current.clear();
        in_string = false;
      }
      continue;
    }

    if (c == '"' || c == '\'') {
      EmitToken(current, segments, lang);
      in_string = true;
      string_char = c;
      current += c;
      continue;
    }

    if (c == ' ' || c == '\t' || !IsIdentifierChar(c)) {
      EmitToken(current, segments, lang);
      segments.push_back(text(std::string(1, c)));
      continue;
    }

    current += c;
  }

  EmitToken(current, segments, lang);

  return hbox(segments);
}

ftxui::Element HighlightWithDef(std::string_view code,
                                const LanguageDef& lang) {
  auto lines = SplitLines(code);
  ftxui::Elements line_elements;
  for (const auto& line : lines) {
    line_elements.push_back(HighlightLine(line, lang));
  }
  return ftxui::vbox(line_elements);
}

}  // namespace

ftxui::Element SyntaxHighlighter::Highlight(std::string_view code,
                                            std::string_view language) {
  const auto* lang = FindLanguage(language);
  if (lang == nullptr) {
    auto lines = SplitLines(code);
    ftxui::Elements line_elements;
    for (const auto& line : lines) {
      line_elements.push_back(ftxui::text(line));
    }
    return ftxui::vbox(line_elements);
  }
  return HighlightWithDef(code, *lang);
}

}  // namespace yac::presentation::syntax
