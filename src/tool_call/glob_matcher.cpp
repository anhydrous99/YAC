#include "tool_call/glob_matcher.hpp"

#include <regex>
#include <string>
#include <string_view>

namespace yac::tool_call {

std::string GlobToRegex(std::string_view glob) {
  std::string result = "^";
  size_t i = 0;
  while (i < glob.size()) {
    char c = glob[i];
    if (c == '*') {
      if (i + 1 < glob.size() && glob[i + 1] == '*') {
        if (i + 2 < glob.size() && glob[i + 2] == '/') {
          result += "(?:.*/)?";
          i += 3;
        } else {
          result += ".*";
          i += 2;
        }
      } else {
        result += "[^/]*";
        i += 1;
      }
    } else if (c == '?') {
      result += "[^/]";
      i += 1;
    } else if (c == '^' || c == '$' || c == '.' || c == '|' || c == '+' ||
               c == '(' || c == ')' || c == '{' || c == '}' || c == '[' ||
               c == ']' || c == '\\') {
      result += '\\';
      result += c;
      i += 1;
    } else {
      result += c;
      i += 1;
    }
  }
  result += "$";
  return result;
}

CompiledGlob::CompiledGlob(std::string_view glob)
    : re_(GlobToRegex(glob), std::regex::ECMAScript) {}

bool CompiledGlob::Match(std::string_view path) const {
  return std::regex_match(std::string(path), re_);
}

bool MatchesGlob(std::string_view path, std::string_view glob) {
  return CompiledGlob(glob).Match(path);
}

}  // namespace yac::tool_call
