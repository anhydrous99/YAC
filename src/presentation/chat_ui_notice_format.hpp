#pragma once

#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/color.hpp"
#include "ui_status.hpp"

#include <string>

namespace yac::presentation::detail {

std::string FormatTokens(int tokens);
std::string FormatPercent(double percent);
ftxui::Color PercentColor(double percent);
bool IsWhitespaceOnly(const std::string& value);
std::string SeverityLabel(UiSeverity severity);
ftxui::Color SeverityColor(UiSeverity severity);
std::string NoticeText(const UiNotice& notice);
ftxui::Element NoticeLine(const UiNotice& notice);

}  // namespace yac::presentation::detail
