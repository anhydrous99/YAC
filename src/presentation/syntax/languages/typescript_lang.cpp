#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

const LanguageDef kTypescriptLanguage = {
    .name = "typescript",
    .keywords = {"abstract",  "any",        "as",        "async",
                 "await",     "boolean",    "break",     "case",
                 "catch",     "class",      "const",     "constructor",
                 "continue",  "debugger",   "declare",   "default",
                 "delete",    "do",         "else",      "enum",
                 "export",    "extends",    "false",     "finally",
                 "for",       "from",       "function",  "get",
                 "if",        "implements", "import",    "in",
                 "infer",     "instanceof", "interface", "is",
                 "keyof",     "let",        "module",    "namespace",
                 "never",     "new",        "null",      "number",
                 "of",        "package",    "private",   "protected",
                 "public",    "readonly",   "require",   "return",
                 "set",       "static",     "string",    "super",
                 "switch",    "symbol",     "this",      "throw",
                 "true",      "try",        "type",      "typeof",
                 "undefined", "unique",     "unknown",   "var",
                 "void",      "while",      "with",      "yield"},
    .types = {"Array",    "BigInt",  "Boolean",       "Date",     "Error",
              "Function", "Map",     "Number",        "Object",   "Partial",
              "Pick",     "Promise", "ReadonlyArray", "Readonly", "Record",
              "Required", "RegExp",  "Set",           "String",   "Symbol",
              "WeakMap",  "WeakSet"},
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

const LanguageDef& TypescriptLanguageDef() {
  return kTypescriptLanguage;
}

}  // namespace yac::presentation::syntax
