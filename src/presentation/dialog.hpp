#pragma once

#include "ftxui/component/component.hpp"

#include <string>

namespace yac::presentation {

ftxui::Component DialogModal(ftxui::Component main, ftxui::Component modal,
                             const bool* show);

ftxui::Component DialogPanel(std::string title, ftxui::Component inner_content,
                             bool* show);

}  // namespace yac::presentation
