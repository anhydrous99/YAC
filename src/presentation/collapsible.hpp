#pragma once

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"

#include <functional>
#include <string>

namespace yac::presentation {

ftxui::Component Collapsible(std::string header_text, ftxui::Component content,
                             bool* expanded, std::string summary = "",
                             ftxui::Element peek = ftxui::Element{});

ftxui::Component Collapsible(std::function<std::string()> header_provider,
                             ftxui::Component content, bool* expanded,
                             std::function<std::string()> summary_provider,
                             ftxui::Element peek = ftxui::Element{});

}  // namespace yac::presentation
