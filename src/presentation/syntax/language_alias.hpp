#pragma once

#include <string>
#include <string_view>

namespace yac::presentation::syntax {

[[nodiscard]] std::string CanonicalLanguage(std::string_view name);

[[nodiscard]] std::string LanguageForExtension(std::string_view path);

}  // namespace yac::presentation::syntax
