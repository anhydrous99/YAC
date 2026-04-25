#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

const LanguageDef kGoLanguage = {
    .name = "go",
    .keywords = {"break",     "case",   "chan",    "const",       "continue",
                 "default",   "defer",  "else",    "fallthrough", "for",
                 "func",      "go",     "goto",    "if",          "import",
                 "interface", "map",    "package", "range",       "return",
                 "select",    "struct", "switch",  "type",        "var",
                 "true",      "false",  "nil",     "iota"},
    .types = {"bool",    "byte",    "complex64", "complex128", "error",
              "float32", "float64", "int",       "int8",       "int16",
              "int32",   "int64",   "rune",      "string",     "uint",
              "uint8",   "uint16",  "uint32",    "uint64",     "uintptr",
              "any"},
    .single_line_comment = "//",
    .multi_line_comment_open = "/*",
    .multi_line_comment_close = "*/",
    .string_rules = {{.opener = "`",
                      .closer = "`",
                      .allow_escapes = false,
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
    .decorator_prefix = 0,
    .number_underscores = true,
    .number_hex_bin_oct = true,
    .variable_dollar = false,
};

}  // namespace

const LanguageDef& GoLanguageDef() {
  return kGoLanguage;
}

}  // namespace yac::presentation::syntax
