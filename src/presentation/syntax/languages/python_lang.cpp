#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

const LanguageDef kPythonLanguage = {
    .name = "python",
    .keywords = {"and",    "as",       "assert",  "async", "await",  "break",
                 "class",  "continue", "def",     "del",   "elif",   "else",
                 "except", "False",    "finally", "for",   "from",   "global",
                 "if",     "import",   "in",      "is",    "lambda", "match",
                 "None",   "nonlocal", "not",     "or",    "pass",   "raise",
                 "return", "True",     "try",     "while", "with",   "yield"},
    .types = {"bool", "bytearray", "bytes", "complex", "dict", "float",
              "frozenset", "int", "list", "memoryview", "object", "range",
              "set", "str", "tuple", "type"},
    .single_line_comment = "#",
    .multi_line_comment_open = "",
    .multi_line_comment_close = "",
    .string_rules = {{.opener = R"(""")",
                      .closer = R"(""")",
                      .allow_escapes = true,
                      .multiline = true},
                     {.opener = "'''",
                      .closer = "'''",
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

const LanguageDef& PythonLanguageDef() {
  return kPythonLanguage;
}

}  // namespace yac::presentation::syntax
