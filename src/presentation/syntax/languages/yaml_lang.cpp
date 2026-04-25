#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

const LanguageDef kYamlLanguage = {
    .name = "yaml",
    .keywords = {"true", "false", "null", "yes", "no", "on", "off"},
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
    .variable_dollar = false,
};

}  // namespace

const LanguageDef& YamlLanguageDef() {
  return kYamlLanguage;
}

}  // namespace yac::presentation::syntax
