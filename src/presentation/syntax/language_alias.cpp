#include "language_alias.hpp"

#include "util/string_util.hpp"

#include <string>
#include <unordered_map>

namespace yac::presentation::syntax {

namespace {

const std::unordered_map<std::string, std::string> kAliasMap = {
    {"cpp", "cpp"},
    {"c++", "cpp"},
    {"c", "cpp"},
    {"cc", "cpp"},
    {"cxx", "cpp"},
    {"h", "cpp"},
    {"hpp", "cpp"},
    {"hxx", "cpp"},

    {"python", "python"},
    {"py", "python"},

    {"javascript", "javascript"},
    {"js", "javascript"},
    {"jsx", "javascript"},
    {"mjs", "javascript"},
    {"cjs", "javascript"},

    {"typescript", "typescript"},
    {"ts", "typescript"},
    {"tsx", "typescript"},

    {"rust", "rust"},
    {"rs", "rust"},

    {"go", "go"},
    {"golang", "go"},

    {"bash", "bash"},
    {"sh", "bash"},
    {"zsh", "bash"},
    {"shell", "bash"},

    {"json", "json"},

    {"yaml", "yaml"},
    {"yml", "yaml"},

    {"diff", "diff"},
    {"patch", "diff"},
};

const std::unordered_map<std::string, std::string> kExtensionMap = {
    {"cpp", "cpp"},        {"cc", "cpp"},         {"cxx", "cpp"},
    {"c", "cpp"},          {"h", "cpp"},          {"hpp", "cpp"},
    {"hxx", "cpp"},

    {"py", "python"},

    {"js", "javascript"},  {"mjs", "javascript"}, {"cjs", "javascript"},
    {"jsx", "javascript"},

    {"ts", "typescript"},  {"tsx", "typescript"},

    {"rs", "rust"},

    {"go", "go"},

    {"sh", "bash"},        {"bash", "bash"},      {"zsh", "bash"},

    {"json", "json"},

    {"yaml", "yaml"},      {"yml", "yaml"},

    {"diff", "diff"},      {"patch", "diff"},
};

}  // namespace

std::string CanonicalLanguage(std::string_view name) {
  if (name.empty()) {
    return {};
  }
  auto lower = ::yac::util::ToLowerAscii(name);
  auto it = kAliasMap.find(lower);
  if (it == kAliasMap.end()) {
    return {};
  }
  return it->second;
}

std::string LanguageForExtension(std::string_view path) {
  auto dot = path.rfind('.');
  if (dot == std::string_view::npos || dot + 1 >= path.size()) {
    return {};
  }
  auto ext = ::yac::util::ToLowerAscii(path.substr(dot + 1));
  auto it = kExtensionMap.find(ext);
  if (it == kExtensionMap.end()) {
    return {};
  }
  return it->second;
}

}  // namespace yac::presentation::syntax
