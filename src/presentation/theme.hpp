#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace yac::presentation::theme {

// ---------------------------------------------------------------------------
// Role colors
// ---------------------------------------------------------------------------
inline constexpr ftxui::Color kUserColor =
    ftxui::Color::RGB(137, 180, 250);  // blue
inline constexpr ftxui::Color kAgentColor =
    ftxui::Color::RGB(166, 227, 161);  // green

// ---------------------------------------------------------------------------
// Markdown colors
// ---------------------------------------------------------------------------
inline constexpr ftxui::Color kHeadingColor =
    ftxui::Color::RGB(205, 214, 244);  // bright white
inline constexpr ftxui::Color kLinkColor =
    ftxui::Color::RGB(116, 199, 236);  // cyan-blue
inline constexpr ftxui::Color kQuoteBorderColor =
    ftxui::Color::RGB(250, 179, 135);  // peach

// ---------------------------------------------------------------------------
// Code colors
// ---------------------------------------------------------------------------
inline constexpr ftxui::Color kCodeBg =
    ftxui::Color::RGB(30, 30, 46);  // dark surface
inline constexpr ftxui::Color kCodeFg =
    ftxui::Color::RGB(205, 214, 244);  // light text
inline constexpr ftxui::Color kInlineCodeBg =
    ftxui::Color::RGB(49, 50, 68);  // surface0
inline constexpr ftxui::Color kInlineCodeFg =
    ftxui::Color::RGB(235, 160, 172);  // pink

// ---------------------------------------------------------------------------
// Syntax highlighting colors
// ---------------------------------------------------------------------------
inline constexpr ftxui::Color kKeywordColor =
    ftxui::Color::RGB(203, 166, 247);  // mauve
inline constexpr ftxui::Color kStringColor =
    ftxui::Color::RGB(166, 227, 161);  // green
inline constexpr ftxui::Color kCommentColor =
    ftxui::Color::RGB(108, 112, 134);  // overlay0
inline constexpr ftxui::Color kNumberColor =
    ftxui::Color::RGB(250, 179, 135);  // peach
inline constexpr ftxui::Color kTypeColor =
    ftxui::Color::RGB(249, 226, 175);  // yellow
inline constexpr ftxui::Color kFunctionColor =
    ftxui::Color::RGB(137, 180, 250);  // blue

// ---------------------------------------------------------------------------
// UI chrome colors
// ---------------------------------------------------------------------------
inline constexpr ftxui::Color kBorderColor =
    ftxui::Color::RGB(88, 91, 112);  // surface2
inline constexpr ftxui::Color kDimText =
    ftxui::Color::RGB(108, 112, 134);  // overlay0
inline constexpr ftxui::Color kPromptColor =
    ftxui::Color::RGB(137, 180, 250);  // blue

}  // namespace yac::presentation::theme
