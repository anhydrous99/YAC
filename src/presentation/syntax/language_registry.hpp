#pragma once

#include "language_def.hpp"

#include <string_view>

namespace yac::presentation::syntax {

[[nodiscard]] const LanguageDef* FindLanguage(std::string_view name);

}  // namespace yac::presentation::syntax
