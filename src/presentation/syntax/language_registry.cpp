#include "language_registry.hpp"

#include "language_alias.hpp"

#include <unordered_map>

namespace yac::presentation::syntax {

// Per-language definitions live in src/presentation/syntax/languages/*.cpp.
const LanguageDef& CppLanguageDef();
const LanguageDef& PythonLanguageDef();
const LanguageDef& JavascriptLanguageDef();
const LanguageDef& TypescriptLanguageDef();
const LanguageDef& RustLanguageDef();
const LanguageDef& GoLanguageDef();
const LanguageDef& BashLanguageDef();
const LanguageDef& JsonLanguageDef();
const LanguageDef& YamlLanguageDef();
const LanguageDef& DiffLanguageDef();

namespace {

const std::unordered_map<std::string, const LanguageDef*>& Registry() {
  static const std::unordered_map<std::string, const LanguageDef*> registry = {
      {"cpp", &CppLanguageDef()},
      {"python", &PythonLanguageDef()},
      {"javascript", &JavascriptLanguageDef()},
      {"typescript", &TypescriptLanguageDef()},
      {"rust", &RustLanguageDef()},
      {"go", &GoLanguageDef()},
      {"bash", &BashLanguageDef()},
      {"json", &JsonLanguageDef()},
      {"yaml", &YamlLanguageDef()},
      {"diff", &DiffLanguageDef()},
  };
  return registry;
}

}  // namespace

const LanguageDef* FindLanguage(std::string_view name) {
  auto canonical = CanonicalLanguage(name);
  if (canonical.empty()) {
    return nullptr;
  }
  const auto& map = Registry();
  auto it = map.find(canonical);
  if (it == map.end()) {
    return nullptr;
  }
  return it->second;
}

}  // namespace yac::presentation::syntax
