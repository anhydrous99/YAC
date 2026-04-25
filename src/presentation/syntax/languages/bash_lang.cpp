#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

const LanguageDef kBashLanguage = {
    .name = "bash",
    .keywords = {"if",     "then",    "elif",     "else",     "fi",
                 "case",   "esac",    "for",      "while",    "until",
                 "do",     "done",    "in",       "select",   "function",
                 "return", "break",   "continue", "exit",     "trap",
                 "set",    "unset",   "export",   "readonly", "declare",
                 "local",  "typeset", "true",     "false",    "shift",
                 "source", "alias",   "unalias",  "test",     "echo",
                 "printf", "read"},
    .types = {},
    .single_line_comment = "#",
    .multi_line_comment_open = "",
    .multi_line_comment_close = "",
    .string_rules = {{.opener = "\"",
                      .closer = "\"",
                      .allow_escapes = true,
                      .multiline = false},
                     {.opener = "'",
                      .closer = "'",
                      .allow_escapes = false,
                      .multiline = false}},
    .preprocessor_prefix = "",
    .decorator_prefix = 0,
    .number_underscores = false,
    .number_hex_bin_oct = false,
    .variable_dollar = true,
};

}  // namespace

const LanguageDef& BashLanguageDef() {
  return kBashLanguage;
}

}  // namespace yac::presentation::syntax
