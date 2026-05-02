#include "chat_ui_notice_format.hpp"

#include "theme.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace yac::presentation::detail {

std::string FormatTokens(int tokens) {
  if (tokens < 1000) {
    return std::to_string(tokens);
  }
  if (tokens < 10000) {
    const int whole = tokens / 1000;
    const int tenths = (tokens % 1000) / 100;
    return std::to_string(whole) + "." + std::to_string(tenths) + "k";
  }
  return std::to_string(tokens / 1000) + "k";
}

std::string FormatPercent(double percent) {
  const int whole = static_cast<int>(percent);
  const int tenths = static_cast<int>(percent * 10) % 10;
  return std::to_string(whole) + "." + std::to_string(tenths) + "%";
}

ftxui::Color PercentColor(double percent) {
  const auto& t = theme::CurrentTheme();
  if (percent <= 50.0) {
    return t.role.agent;
  }
  if (percent <= 80.0) {
    return ftxui::Color::Yellow;
  }
  return t.role.error;
}

bool IsWhitespaceOnly(const std::string& value) {
  return std::ranges::all_of(
      value, [](unsigned char ch) { return std::isspace(ch) != 0; });
}

std::string SeverityLabel(UiSeverity severity) {
  switch (severity) {
    case UiSeverity::Info:
      return "info";
    case UiSeverity::Warning:
      return "warning";
    case UiSeverity::Error:
      return "error";
  }
  return "info";
}

ftxui::Color SeverityColor(UiSeverity severity) {
  switch (severity) {
    case UiSeverity::Info:
      return theme::CurrentTheme().chrome.dim_text;
    case UiSeverity::Warning:
      return ftxui::Color::Yellow;
    case UiSeverity::Error:
      return theme::CurrentTheme().role.error;
  }
  return theme::CurrentTheme().chrome.dim_text;
}

std::string NoticeText(const UiNotice& notice) {
  if (notice.detail.empty()) {
    return notice.title;
  }
  return notice.title + ": " + notice.detail;
}

ftxui::Element NoticeLine(const UiNotice& notice) {
  return ftxui::paragraph("  " + SeverityLabel(notice.severity) + ": " +
                          NoticeText(notice)) |
         ftxui::color(SeverityColor(notice.severity));
}

}  // namespace yac::presentation::detail
