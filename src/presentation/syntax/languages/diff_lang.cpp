#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

// Marker definition. The diff lexer dispatches on `lang.name == "diff"` and
// runs a per-line classifier; the keyword/type/comment fields aren't used.
const LanguageDef kDiffLanguage = {
    .name = "diff",
    .keywords = {},
    .types = {},
    .single_line_comment = "",
    .multi_line_comment_open = "",
    .multi_line_comment_close = "",
    .string_rules = {},
    .preprocessor_prefix = "",
    .decorator_prefix = 0,
    .number_underscores = false,
    .number_hex_bin_oct = false,
    .variable_dollar = false,
};

}  // namespace

const LanguageDef& DiffLanguageDef() {
  return kDiffLanguage;
}

}  // namespace yac::presentation::syntax
