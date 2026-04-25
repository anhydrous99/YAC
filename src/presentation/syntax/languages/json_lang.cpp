#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

const LanguageDef kJsonLanguage = {
    .name = "json",
    .keywords = {"true", "false", "null"},
    .types = {},
    .single_line_comment = "",
    .multi_line_comment_open = "",
    .multi_line_comment_close = "",
    .string_rules = {{.opener = "\"",
                      .closer = "\"",
                      .allow_escapes = true,
                      .multiline = false}},
    .preprocessor_prefix = "",
    .decorator_prefix = 0,
    .number_underscores = false,
    .number_hex_bin_oct = false,
    .variable_dollar = false,
};

}  // namespace

const LanguageDef& JsonLanguageDef() {
  return kJsonLanguage;
}

}  // namespace yac::presentation::syntax
