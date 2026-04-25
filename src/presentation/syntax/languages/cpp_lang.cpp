#include "../language_def.hpp"

namespace yac::presentation::syntax {

namespace {

const LanguageDef kCppLanguage = {
    .name = "cpp",
    .keywords =
        {"alignas",       "alignof",     "and",          "asm",
         "auto",          "break",       "case",         "catch",
         "class",         "co_await",    "co_return",    "co_yield",
         "concept",       "const",       "consteval",    "constexpr",
         "constinit",     "const_cast",  "continue",     "decltype",
         "default",       "delete",      "do",           "dynamic_cast",
         "else",          "enum",        "explicit",     "export",
         "extern",        "false",       "final",        "for",
         "friend",        "goto",        "if",           "import",
         "inline",        "module",      "mutable",      "namespace",
         "new",           "noexcept",    "not",          "nullptr",
         "operator",      "or",          "override",     "private",
         "protected",     "public",      "register",     "reinterpret_cast",
         "requires",      "return",      "sizeof",       "static",
         "static_assert", "static_cast", "struct",       "switch",
         "template",      "this",        "thread_local", "throw",
         "true",          "try",         "typedef",      "typeid",
         "typename",      "union",       "using",        "virtual",
         "volatile",      "while",       "xor"},
    .types = {"bool",     "char",     "char8_t",     "char16_t",   "char32_t",
              "double",   "float",    "int",         "int8_t",     "int16_t",
              "int32_t",  "int64_t",  "long",        "short",      "signed",
              "size_t",   "string",   "string_view", "uint8_t",    "uint16_t",
              "uint32_t", "uint64_t", "unsigned",    "void",       "wchar_t",
              "vector",   "map",      "set",         "unique_ptr", "shared_ptr",
              "weak_ptr", "optional", "variant",     "tuple",      "array",
              "span",     "function"},
    .single_line_comment = "//",
    .multi_line_comment_open = "/*",
    .multi_line_comment_close = "*/",
    .string_rules = {{.opener = "R\"(",
                      .closer = ")\"",
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
    .preprocessor_prefix = "#",
    .decorator_prefix = 0,
    .number_underscores = true,
    .number_hex_bin_oct = true,
    .variable_dollar = false,
};

}  // namespace

const LanguageDef& CppLanguageDef() {
  return kCppLanguage;
}

}  // namespace yac::presentation::syntax
