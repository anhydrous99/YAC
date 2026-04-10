#include "renderer.hpp"

#include "../theme.hpp"
#include "../util/string_util.hpp"

#include <string>
#include <type_traits>
#include <utility>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {

namespace {

inline const auto& k_theme = theme::Theme::Instance();

ftxui::Element RenderCodeText(const std::string& text) {
  return ftxui::paragraph(text) | ftxui::bgcolor(k_theme.code.inline_bg) |
         ftxui::color(k_theme.code.inline_fg);
}

ftxui::Element RenderLabelValue(const std::string& label,
                                const std::string& value) {
  return ftxui::hbox(
      {ftxui::text(label) | ftxui::bold | ftxui::color(k_theme.chrome.dim_text),
       ftxui::paragraph(value) | ftxui::color(k_theme.chrome.body_text) |
           ftxui::flex});
}

ftxui::Element RenderWrappedLine(
    const std::string& text, ftxui::Color color = k_theme.chrome.body_text) {
  return ftxui::paragraph(text) | ftxui::color(color);
}

ftxui::Element RenderContainer(const std::string& icon,
                               const std::string& label, ftxui::Color accent,
                               ftxui::Elements content,
                               ftxui::Color border_color = ftxui::Color{}) {
  if (border_color == ftxui::Color()) {
    border_color = accent;
  }

  auto header = ftxui::hbox({
                    ftxui::text(" " + icon + " ") | ftxui::bold |
                        ftxui::color(k_theme.tool.icon_fg),
                    ftxui::text(label) | ftxui::bold | ftxui::color(accent),
                }) |
                ftxui::bgcolor(k_theme.tool.header_bg);

  auto body =
      ftxui::vbox(std::move(content)) | ftxui::color(k_theme.chrome.body_text);

  return ftxui::vbox({header, body}) | ftxui::borderRounded |
         ftxui::color(border_color);
}

ftxui::Element RenderLines(const std::vector<std::string>& lines,
                           const std::string& empty_text = "") {
  if (lines.empty()) {
    return empty_text.empty() ? ftxui::text("")
                              : ftxui::text(empty_text) |
                                    ftxui::color(k_theme.chrome.dim_text);
  }

  ftxui::Elements elements;
  for (const auto& line : lines) {
    elements.push_back(RenderWrappedLine(line));
  }
  return ftxui::vbox(std::move(elements));
}

}  // namespace

ftxui::Element ToolCallRenderer::Render(const ToolCallBlock& block) {
  return std::visit(
      [](const auto& call) -> ftxui::Element {
        using T = std::decay_t<decltype(call)>;
        if constexpr (std::is_same_v<T, BashCall>) {
          return RenderBash(call);
        } else if constexpr (std::is_same_v<T, FileEditCall>) {
          return RenderFileEdit(call);
        } else if constexpr (std::is_same_v<T, FileReadCall>) {
          return RenderFileRead(call);
        } else if constexpr (std::is_same_v<T, GrepCall>) {
          return RenderGrep(call);
        } else if constexpr (std::is_same_v<T, GlobCall>) {
          return RenderGlob(call);
        } else if constexpr (std::is_same_v<T, WebFetchCall>) {
          return RenderWebFetch(call);
        } else if constexpr (std::is_same_v<T, WebSearchCall>) {
          return RenderWebSearch(call);
        } else {
          return ftxui::text("");
        }
      },
      block);
}

ftxui::Element ToolCallRenderer::RenderBash(const BashCall& call) {
  ftxui::Elements content;
  content.push_back(RenderLabelValue("Command: ", call.command));
  content.push_back(RenderCodeText(call.command));

  if (call.exit_code != 0) {
    content.push_back(
        RenderWrappedLine("Exit code: " + std::to_string(call.exit_code),
                          k_theme.tool.edit_remove));
  }

  if (!call.output.empty()) {
    content.push_back(RenderLabelValue("Output: ", ""));
    content.push_back(RenderLines(util::SplitLines(call.output)));
  }

  auto border =
      call.is_error ? k_theme.tool.edit_remove : k_theme.tool.bash_accent;
  return RenderContainer("#", "bash", k_theme.tool.bash_accent,
                         std::move(content), border);
}

ftxui::Element ToolCallRenderer::RenderFileEdit(const FileEditCall& call) {
  ftxui::Elements content;
  content.push_back(RenderLabelValue("File: ", call.filepath));

  if (call.diff.empty()) {
    content.push_back(ftxui::text("No diff lines") |
                      ftxui::color(k_theme.chrome.dim_text));
  } else {
    for (const auto& line : call.diff) {
      std::string prefix = "  ";
      ftxui::Color color = k_theme.tool.edit_context;
      if (line.type == DiffLine::Add) {
        prefix = "+ ";
        color = k_theme.tool.edit_add;
      } else if (line.type == DiffLine::Remove) {
        prefix = "- ";
        color = k_theme.tool.edit_remove;
      }
      content.push_back(RenderWrappedLine(prefix + line.content, color));
    }
  }

  return RenderContainer("→", "edit", k_theme.tool.edit_context,
                         std::move(content));
}

ftxui::Element ToolCallRenderer::RenderFileRead(const FileReadCall& call) {
  ftxui::Elements content;
  content.push_back(RenderLabelValue("File: ", call.filepath));
  content.push_back(RenderWrappedLine(
      "Loaded " + std::to_string(call.lines_loaded) + " lines",
      k_theme.tool.read_accent));
  if (!call.excerpt.empty()) {
    content.push_back(RenderWrappedLine(call.excerpt));
  }

  return RenderContainer("◆", "read", k_theme.tool.read_accent,
                         std::move(content));
}

ftxui::Element ToolCallRenderer::RenderGrep(const GrepCall& call) {
  ftxui::Elements content;
  content.push_back(RenderLabelValue("Pattern: ", call.pattern));
  content.push_back(RenderCodeText(call.pattern));
  content.push_back(RenderWrappedLine(
      std::to_string(call.match_count) + " matches", k_theme.tool.grep_accent));

  for (const auto& match : call.matches) {
    content.push_back(
        RenderWrappedLine(match.filepath + ":" + std::to_string(match.line)));
    content.push_back(
        RenderWrappedLine(match.content, k_theme.chrome.dim_text));
  }

  return RenderContainer("⊕", "grep", k_theme.tool.grep_accent,
                         std::move(content));
}

ftxui::Element ToolCallRenderer::RenderGlob(const GlobCall& call) {
  ftxui::Elements content;
  content.push_back(RenderLabelValue("Pattern: ", call.pattern));
  content.push_back(RenderCodeText(call.pattern));

  if (call.matched_files.empty()) {
    content.push_back(ftxui::text("No matches") |
                      ftxui::color(k_theme.chrome.dim_text));
  } else {
    for (const auto& filepath : call.matched_files) {
      content.push_back(RenderWrappedLine(filepath, k_theme.tool.glob_accent));
    }
  }

  return RenderContainer("⊙", "glob", k_theme.tool.glob_accent,
                         std::move(content));
}

ftxui::Element ToolCallRenderer::RenderWebFetch(const WebFetchCall& call) {
  ftxui::Elements content;
  content.push_back(RenderLabelValue("URL: ", call.url));
  if (!call.title.empty()) {
    content.push_back(RenderWrappedLine(call.title, k_theme.tool.web_accent));
  }
  if (!call.excerpt.empty()) {
    content.push_back(RenderWrappedLine(call.excerpt));
  }

  return RenderContainer("↗", "fetch", k_theme.tool.web_accent,
                         std::move(content));
}

ftxui::Element ToolCallRenderer::RenderWebSearch(const WebSearchCall& call) {
  ftxui::Elements content;
  content.push_back(RenderLabelValue("Query: ", call.query));

  if (call.results.empty()) {
    content.push_back(ftxui::text("No results") |
                      ftxui::color(k_theme.chrome.dim_text));
  } else {
    for (const auto& entry : call.results) {
      content.push_back(
          RenderWrappedLine(entry.title, k_theme.tool.web_accent));
      content.push_back(RenderWrappedLine(entry.url, k_theme.chrome.dim_text));
      if (!entry.snippet.empty()) {
        content.push_back(RenderWrappedLine(entry.snippet));
      }
    }
  }

  return RenderContainer("◎", "search", k_theme.tool.web_accent,
                         std::move(content));
}

}  // namespace yac::presentation::tool_call
