#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

const LanguageDef kJavascriptLanguage = {
    .name = "javascript",
    .keywords = {"async",  "await",   "break",    "case",     "catch",
                 "class",  "const",   "continue", "debugger", "default",
                 "delete", "do",      "else",     "export",   "extends",
                 "false",  "finally", "for",      "from",     "function",
                 "get",    "if",      "import",   "in",       "instanceof",
                 "let",    "new",     "null",     "of",       "return",
                 "set",    "static",  "super",    "switch",   "this",
                 "throw",  "true",    "try",      "typeof",   "undefined",
                 "var",    "void",    "while",    "with",     "yield"},
    .types = {"Array", "BigInt", "Boolean", "Date", "Error", "Function", "Map",
              "Number", "Object", "Promise", "RegExp", "Set", "String",
              "Symbol", "WeakMap", "WeakSet"},
    .single_line_comment = "//",
    .multi_line_comment_open = "/*",
    .multi_line_comment_close = "*/",
    .string_rules = {{.opener = "`",
                      .closer = "`",
                      .allow_escapes = true,
                      .multiline = true},
                     {.opener = "\"",
                      .closer = "\"",
                      .allow_escapes = true,
                      .multiline = false},
                     {.opener = "'",
                      .closer = "'",
                      .allow_escapes = true,
                      .multiline = false}},
    .preprocessor_prefix = "",
    .decorator_prefix = '@',
    .number_underscores = true,
    .number_hex_bin_oct = true,
    .variable_dollar = false,
};

}  // namespace

const LanguageDef& JavascriptLanguageDef() {
  return kJavascriptLanguage;
}

}  // namespace yac::presentation::syntax
