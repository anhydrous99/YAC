#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

const LanguageDef kRustLanguage = {
    .name = "rust",
    .keywords = {"as",    "async",  "await", "break", "const",  "continue",
                 "crate", "dyn",    "else",  "enum",  "extern", "false",
                 "fn",    "for",    "if",    "impl",  "in",     "let",
                 "loop",  "match",  "mod",   "move",  "mut",    "pub",
                 "ref",   "return", "Self",  "self",  "static", "struct",
                 "super", "trait",  "true",  "type",  "unsafe", "use",
                 "where", "while",  "yield"},
    .types = {"bool",   "char",   "f32",     "f64",   "i8",      "i16",
              "i32",    "i64",    "i128",    "isize", "str",     "u8",
              "u16",    "u32",    "u64",     "u128",  "usize",   "Box",
              "Option", "Result", "String",  "Vec",   "HashMap", "HashSet",
              "Rc",     "Arc",    "RefCell", "Cell"},
    .single_line_comment = "//",
    .multi_line_comment_open = "/*",
    .multi_line_comment_close = "*/",
    .string_rules = {{.opener = "r#\"",
                      .closer = "\"#",
                      .allow_escapes = false,
                      .multiline = true},
                     {.opener = "r\"",
                      .closer = "\"",
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

const LanguageDef& RustLanguageDef() {
  return kRustLanguage;
}

}  // namespace yac::presentation::syntax
