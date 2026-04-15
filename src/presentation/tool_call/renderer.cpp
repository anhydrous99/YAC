#include "renderer.hpp"

#include "../theme.hpp"
#include "../util/string_util.hpp"

#include <string>
#include <type_traits>
#include <utility>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {

namespace tool_data = ::yac::tool_call;

namespace {

ftxui::Element RenderCodeText(const std::string& text,
                              const theme::Theme& theme) {
  return ftxui::paragraph(text) | ftxui::bgcolor(theme.code.inline_bg) |
         ftxui::color(theme.code.inline_fg);
}

ftxui::Element RenderLabelValue(const std::string& label,
                                const std::string& value,
                                const theme::Theme& theme) {
  return ftxui::hbox(
      {ftxui::text(label) | ftxui::bold | ftxui::color(theme.chrome.dim_text),
       ftxui::paragraph(value) | ftxui::color(theme.chrome.body_text) |
           ftxui::flex});
}

ftxui::Element RenderWrappedLine(const std::string& text, ftxui::Color color) {
  return ftxui::paragraph(text) | ftxui::color(color);
}

ftxui::Element RenderContainer(const std::string& icon,
                               const std::string& label, ftxui::Color accent,
                               ftxui::Elements content,
                               const theme::Theme& theme) {
  auto header = ftxui::hbox({
                    ftxui::text(" " + icon + " ") | ftxui::bold |
                        ftxui::color(theme.tool.icon_fg),
                    ftxui::text(label) | ftxui::bold | ftxui::color(accent),
                    ftxui::filler(),
                }) |
                ftxui::bgcolor(theme.tool.header_bg);

  auto body =
      ftxui::vbox(std::move(content)) | ftxui::color(theme.chrome.body_text);

  return ftxui::vbox({
             header,
             ftxui::hbox(
                 {ftxui::text("  "), body | ftxui::flex, ftxui::text("  ")}) |
                 ftxui::bgcolor(theme.cards.agent_bg),
             ftxui::text("") | ftxui::bgcolor(theme.cards.agent_bg),
         }) |
         ftxui::bgcolor(theme.cards.agent_bg);
}

ftxui::Element RenderLines(const std::vector<std::string>& lines,
                           const theme::Theme& theme,
                           const std::string& empty_text = "") {
  if (lines.empty()) {
    return empty_text.empty()
               ? ftxui::text("")
               : ftxui::text(empty_text) | ftxui::color(theme.chrome.dim_text);
  }

  ftxui::Elements elements;
  for (const auto& line : lines) {
    elements.push_back(RenderWrappedLine(line, theme.chrome.body_text));
  }
  return ftxui::vbox(std::move(elements));
}

}  // namespace

ftxui::Element ToolCallRenderer::Render(const tool_data::ToolCallBlock& block) {
  return Render(block, RenderContext{});
}

ftxui::Element ToolCallRenderer::Render(const tool_data::ToolCallBlock& block,
                                        const RenderContext& context) {
  return std::visit(
      [&context](const auto& call) -> ftxui::Element {
        using T = std::decay_t<decltype(call)>;
        if constexpr (std::is_same_v<T, tool_data::BashCall>) {
          return RenderBash(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::FileEditCall>) {
          return RenderFileEdit(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::FileReadCall>) {
          return RenderFileRead(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::GrepCall>) {
          return RenderGrep(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::GlobCall>) {
          return RenderGlob(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::WebFetchCall>) {
          return RenderWebFetch(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::WebSearchCall>) {
          return RenderWebSearch(call, context);
        } else {
          return ftxui::text("");
        }
      },
      block);
}

ftxui::Element ToolCallRenderer::RenderBash(const tool_data::BashCall& call,
                                            const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(RenderLabelValue("Command: ", call.command, theme));
  content.push_back(RenderCodeText(call.command, theme));

  if (call.exit_code != 0) {
    content.push_back(
        RenderWrappedLine("Exit code: " + std::to_string(call.exit_code),
                          theme.tool.edit_remove));
  }

  if (!call.output.empty()) {
    content.push_back(RenderLabelValue("Output: ", "", theme));
    content.push_back(RenderLines(util::SplitLines(call.output), theme));
  }

  auto accent = call.is_error ? theme.tool.edit_remove : theme.tool.bash_accent;
  return RenderContainer("#", "bash", accent, std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderFileEdit(
    const tool_data::FileEditCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(RenderLabelValue("File: ", call.filepath, theme));

  if (call.diff.empty()) {
    content.push_back(ftxui::text("No diff lines") |
                      ftxui::color(theme.chrome.dim_text));
  } else {
    for (const auto& line : call.diff) {
      std::string prefix = "  ";
      ftxui::Color color = theme.tool.edit_context;
      if (line.type == tool_data::DiffLine::Add) {
        prefix = "+ ";
        color = theme.tool.edit_add;
      } else if (line.type == tool_data::DiffLine::Remove) {
        prefix = "- ";
        color = theme.tool.edit_remove;
      }
      content.push_back(RenderWrappedLine(prefix + line.content, color));
    }
  }

  return RenderContainer("→", "edit", theme.tool.edit_context,
                         std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderFileRead(
    const tool_data::FileReadCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(RenderLabelValue("File: ", call.filepath, theme));
  content.push_back(RenderWrappedLine(
      "Loaded " + std::to_string(call.lines_loaded) + " lines",
      theme.tool.read_accent));
  if (!call.excerpt.empty()) {
    content.push_back(RenderWrappedLine(call.excerpt, theme.chrome.body_text));
  }

  return RenderContainer("◆", "read", theme.tool.read_accent,
                         std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderGrep(const tool_data::GrepCall& call,
                                            const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(RenderLabelValue("Pattern: ", call.pattern, theme));
  content.push_back(RenderCodeText(call.pattern, theme));
  content.push_back(RenderWrappedLine(
      std::to_string(call.match_count) + " matches", theme.tool.grep_accent));

  for (const auto& match : call.matches) {
    content.push_back(
        RenderWrappedLine(match.filepath + ":" + std::to_string(match.line),
                          theme.chrome.body_text));
    content.push_back(RenderWrappedLine(match.content, theme.chrome.dim_text));
  }

  return RenderContainer("⊕", "grep", theme.tool.grep_accent,
                         std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderGlob(const tool_data::GlobCall& call,
                                            const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(RenderLabelValue("Pattern: ", call.pattern, theme));
  content.push_back(RenderCodeText(call.pattern, theme));

  if (call.matched_files.empty()) {
    content.push_back(ftxui::text("No matches") |
                      ftxui::color(theme.chrome.dim_text));
  } else {
    for (const auto& filepath : call.matched_files) {
      content.push_back(RenderWrappedLine(filepath, theme.tool.glob_accent));
    }
  }

  return RenderContainer("⊙", "glob", theme.tool.glob_accent,
                         std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderWebFetch(
    const tool_data::WebFetchCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(RenderLabelValue("URL: ", call.url, theme));
  if (!call.title.empty()) {
    content.push_back(RenderWrappedLine(call.title, theme.tool.web_accent));
  }
  if (!call.excerpt.empty()) {
    content.push_back(RenderWrappedLine(call.excerpt, theme.chrome.body_text));
  }

  return RenderContainer("↗", "fetch", theme.tool.web_accent,
                         std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderWebSearch(
    const tool_data::WebSearchCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(RenderLabelValue("Query: ", call.query, theme));

  if (call.results.empty()) {
    content.push_back(ftxui::text("No results") |
                      ftxui::color(theme.chrome.dim_text));
  } else {
    for (const auto& entry : call.results) {
      content.push_back(RenderWrappedLine(entry.title, theme.tool.web_accent));
      content.push_back(RenderWrappedLine(entry.url, theme.chrome.dim_text));
      if (!entry.snippet.empty()) {
        content.push_back(
            RenderWrappedLine(entry.snippet, theme.chrome.body_text));
      }
    }
  }

  return RenderContainer("◎", "search", theme.tool.web_accent,
                         std::move(content), theme);
}

}  // namespace yac::presentation::tool_call
