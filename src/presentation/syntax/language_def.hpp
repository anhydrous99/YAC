#pragma once

#include <string>
#include <unordered_set>
#include <vector>

namespace yac::presentation::syntax {

struct StringRule {
  std::string opener;
  std::string closer;
  bool allow_escapes = true;
  bool multiline = false;
};

struct LanguageDef {
  std::string name;
  std::unordered_set<std::string> keywords;
  std::unordered_set<std::string> types;
  std::string single_line_comment;
  std::string multi_line_comment_open;
  std::string multi_line_comment_close;
  std::vector<StringRule> string_rules;
  std::string preprocessor_prefix;
  char decorator_prefix = 0;
  bool number_underscores = true;
  bool number_hex_bin_oct = true;
  bool variable_dollar = false;
};

}  // namespace yac::presentation::syntax
