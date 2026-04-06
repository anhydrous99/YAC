#pragma once

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/color.hpp>

namespace yac::presentation::theme {

// ---------------------------------------------------------------------------
// Role colors
// ---------------------------------------------------------------------------
inline const ftxui::Color KUserColor =
    ftxui::Color::RGB(137, 180, 250);  // blue
inline const ftxui::Color KAgentColor =
    ftxui::Color::RGB(166, 227, 161);  // green

// ---------------------------------------------------------------------------
// Markdown colors
// ---------------------------------------------------------------------------
inline const ftxui::Color KHeadingColor =
    ftxui::Color::RGB(205, 214, 244);  // bright white
inline const ftxui::Color KLinkColor =
    ftxui::Color::RGB(116, 199, 236);  // cyan-blue
inline const ftxui::Color KQuoteBorderColor =
    ftxui::Color::RGB(250, 179, 135);  // peach

// ---------------------------------------------------------------------------
// Code colors
// ---------------------------------------------------------------------------
inline const ftxui::Color KCodeBg =
    ftxui::Color::RGB(30, 30, 46);  // dark surface
inline const ftxui::Color KCodeFg =
    ftxui::Color::RGB(205, 214, 244);  // light text
inline const ftxui::Color KInlineCodeBg =
    ftxui::Color::RGB(49, 50, 68);  // surface0
inline const ftxui::Color KInlineCodeFg =
    ftxui::Color::RGB(235, 160, 172);  // pink

// ---------------------------------------------------------------------------
// Syntax highlighting colors
// ---------------------------------------------------------------------------
inline const ftxui::Color KKeywordColor =
    ftxui::Color::RGB(203, 166, 247);  // mauve
inline const ftxui::Color KStringColor =
    ftxui::Color::RGB(166, 227, 161);  // green
inline const ftxui::Color KCommentColor =
    ftxui::Color::RGB(108, 112, 134);  // overlay0
inline const ftxui::Color KNumberColor =
    ftxui::Color::RGB(250, 179, 135);  // peach
inline const ftxui::Color KTypeColor =
    ftxui::Color::RGB(249, 226, 175);  // yellow
inline const ftxui::Color KFunctionColor =
    ftxui::Color::RGB(137, 180, 250);  // blue

// ---------------------------------------------------------------------------
// UI chrome colors
// ---------------------------------------------------------------------------
inline const ftxui::Color KBorderColor =
    ftxui::Color::RGB(88, 91, 112);  // surface2
inline const ftxui::Color KDimText =
    ftxui::Color::RGB(108, 112, 134);  // overlay0
inline const ftxui::Color KPromptColor =
    ftxui::Color::RGB(137, 180, 250);  // blue

// ---------------------------------------------------------------------------
// Message card colors
// ---------------------------------------------------------------------------
inline const ftxui::Color KUserCardBg =
    ftxui::Color::RGB(30, 30, 46);  // base (dark surface)
inline const ftxui::Color KAgentCardBg =
    ftxui::Color::RGB(24, 24, 37);  // mantle (slightly darker)
inline const ftxui::Color KUserCardBorder =
    ftxui::Color::RGB(69, 71, 90);  // surface1
inline const ftxui::Color KAgentCardBorder =
    ftxui::Color::RGB(69, 71, 90);  // surface1
inline const ftxui::Color KCodeBlockBorder =
    ftxui::Color::RGB(69, 71, 90);                                   // surface1
inline const ftxui::Color KQuoteBg = ftxui::Color::RGB(30, 30, 46);  // base
inline const ftxui::Color KSeparatorColor =
    ftxui::Color::RGB(49, 50, 68);  // surface0

}  // namespace yac::presentation::theme
