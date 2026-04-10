#pragma once

#include "ftxui/component/component.hpp"

#include <string>

namespace yac::presentation {

ftxui::Component Collapsible(std::string header_text, ftxui::Component content,
                             bool* expanded);

}  // namespace yac::presentation
