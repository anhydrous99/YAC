#include "../syntax/highlighter.hpp"
#include "../syntax/language_alias.hpp"
#include "../ui_spacing.hpp"
#include "../util/count_summary.hpp"
#include "../util/string_util.hpp"
#include "renderer.hpp"
#include "renderer_helpers.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {
namespace {

std::string DirectoryEntryTypeLabel(tool_data::DirectoryEntryType type) {
  switch (type) {
    case tool_data::DirectoryEntryType::File:
      return "file";
    case tool_data::DirectoryEntryType::Directory:
      return "dir";
    case tool_data::DirectoryEntryType::Other:
      return "other";
  }
  return "other";
}

}  // namespace

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
  content.push_back(ftxui::text(call.filepath) |
                    ftxui::color(theme.semantic.text_strong));
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else if (call.diff.empty()) {
    content.push_back(ftxui::text("No diff lines") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    auto lexer_handle = MakeLexerForFile(call.filepath);
    auto* lexer_ptr = lexer_handle.Get();

    for (const auto& line : call.diff) {
      ftxui::Color gutter_fg = theme.tool.edit_context;
      ftxui::Color fallback_fg = theme.tool.edit_context;
      std::string gutter = std::string(layout::kCardPadX, ' ');
      bool tint = false;
      if (line.type == tool_data::DiffLine::Add) {
        gutter = "+ ";
        gutter_fg = theme.tool.edit_add;
        fallback_fg = theme.tool.edit_add;
        tint = false;
      } else if (line.type == tool_data::DiffLine::Remove) {
        gutter = "- ";
        gutter_fg = theme.tool.edit_remove;
        fallback_fg = theme.tool.edit_remove;
        tint = false;
      }
      auto body =
          RenderHighlightedLine(lexer_ptr, line.content, context, fallback_fg,
                                theme.tool.header_bg, tint);
      content.push_back(ftxui::hbox({
          ftxui::text(gutter) | ftxui::color(gutter_fg),
          body,
      }));
    }
  }

  return RenderContainer("→", "edit", theme.tool.edit_context,
                         std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderFileRead(
    const tool_data::FileReadCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.filepath) |
                    ftxui::color(theme.semantic.text_strong));
  content.push_back(
      RenderWrappedLine(util::CountSummary(call.lines_loaded, "line", "lines"),
                        theme.semantic.text_muted));
  if (!call.excerpt.empty()) {
    auto language = syntax::LanguageForExtension(call.filepath);
    if (!language.empty()) {
      auto highlighted = syntax::SyntaxHighlighter::HighlightLines(
          call.excerpt, language, context);
      for (auto& line : highlighted) {
        content.push_back(std::move(line));
      }
    } else {
      content.push_back(
          RenderWrappedLine(call.excerpt, theme.semantic.text_body));
    }
  }

  return RenderContainer("◆", "read", theme.tool.read_accent,
                         std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderFileWrite(
    const tool_data::FileWriteCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.filepath) |
                    ftxui::color(theme.semantic.text_strong));
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else if (call.is_streaming) {
    content.push_back(RenderWrappedLine("Streaming…", theme.tool.edit_add));
  } else {
    content.push_back(RenderWrappedLine(
        "Added " + std::to_string(call.lines_added) + " lines, removed " +
            std::to_string(call.lines_removed) + " lines",
        theme.semantic.text_muted));
  }
  if (!call.content_preview.empty()) {
    content.push_back(ftxui::text("Preview:") |
                      ftxui::color(theme.semantic.text_muted));
    const auto lines = util::SplitLines(call.content_preview);
    auto lexer_handle = MakeLexerForFile(call.filepath);
    auto* lexer_ptr = lexer_handle.Get();

    auto render_at = [&](size_t index) -> ftxui::Element {
      return RenderHighlightedLine(lexer_ptr, lines[index], context,
                                   theme.semantic.text_body,
                                   theme.tool.header_bg, false);
    };

    if (call.is_streaming && lines.size() > kMaxPreviewRows) {
      const auto omitted = lines.size() - kMaxPreviewRows;
      content.push_back(
          ftxui::text("… " + std::to_string(omitted) + " earlier lines") |
          ftxui::color(theme.semantic.text_muted));
      // Skipped lines still need to advance lexer state for multiline tokens.
      if (lexer_ptr != nullptr) {
        for (size_t skip = 0; skip < lines.size() - kMaxPreviewRows; ++skip) {
          (void)lexer_ptr->NextLine(lines[skip]);
        }
      }
      for (auto index = lines.size() - kMaxPreviewRows; index < lines.size();
           ++index) {
        content.push_back(render_at(index));
      }
    } else {
      const auto limit = std::min(lines.size(), kMaxPreviewRows);
      for (size_t index = 0; index < limit; ++index) {
        content.push_back(render_at(index));
      }
      AddOmittedRows(content, lines.size(), theme);
    }
  }

  const auto accent =
      call.is_error ? theme.tool.edit_remove : theme.tool.edit_add;
  return RenderContainer("✎", "write", accent, std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderListDir(
    const tool_data::ListDirCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.path) |
                    ftxui::color(theme.semantic.text_strong));
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else if (call.entries.empty()) {
    content.push_back(ftxui::text("No entries") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    content.push_back(RenderWrappedLine(
        util::CountSummary(call.entries.size(), "entry", "entries"),
        theme.semantic.text_muted));
    const auto limit = std::min(call.entries.size(), kMaxPreviewRows);
    for (size_t index = 0; index < limit; ++index) {
      const auto& entry = call.entries[index];
      auto line = DirectoryEntryTypeLabel(entry.type) +
                  std::string(layout::kCardPadX, ' ') + entry.name;
      if (entry.type == tool_data::DirectoryEntryType::File) {
        line += std::string(layout::kCardPadX, ' ') +
                std::to_string(entry.size) + " bytes";
      }
      content.push_back(RenderWrappedLine(line, theme.semantic.text_body));
    }
    AddOmittedRows(content, call.entries.size(), theme);
    if (call.truncated) {
      content.push_back(
          RenderWrappedLine("Result truncated", theme.semantic.text_muted));
    }
  }

  const auto accent =
      call.is_error ? theme.tool.edit_remove : theme.tool.glob_accent;
  return RenderContainer("□", "ls", accent, std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderGrep(const tool_data::GrepCall& call,
                                            const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.pattern) |
                    ftxui::color(theme.semantic.text_strong));
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else {
    content.push_back(RenderWrappedLine(
        util::CountSummary(call.match_count, "match", "matches"),
        theme.semantic.text_muted));

    for (const auto& match : call.matches) {
      content.push_back(
          RenderWrappedLine(match.filepath + ":" + std::to_string(match.line),
                            theme.semantic.text_body));
      content.push_back(
          RenderWrappedLine(match.content, theme.semantic.text_muted));
    }
  }

  return RenderContainer("⊕", "grep", theme.tool.grep_accent,
                         std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderGlob(const tool_data::GlobCall& call,
                                            const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.pattern) |
                    ftxui::color(theme.semantic.text_strong));
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else if (call.matched_files.empty()) {
    content.push_back(ftxui::text("No matches") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    content.push_back(RenderWrappedLine(
        util::CountSummary(call.matched_files.size(), "match", "matches"),
        theme.semantic.text_muted));
    for (const auto& filepath : call.matched_files) {
      content.push_back(RenderWrappedLine(filepath, theme.semantic.text_body));
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

ftxui::Element ToolCallRenderer::RenderSubAgent(
    const tool_data::SubAgentCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  const auto& colors = theme.sub_agent;

  std::string icon;
  ftxui::Color accent;
  switch (call.status) {
    case tool_data::SubAgentStatus::Running:
      icon = ">";
      accent = colors.running_accent;
      break;
    case tool_data::SubAgentStatus::Complete:
      icon = "v";
      accent = colors.success_accent;
      break;
    case tool_data::SubAgentStatus::Error:
      icon = "x";
      accent = colors.error_accent;
      break;
    case tool_data::SubAgentStatus::Timeout:
      icon = "~";
      accent = colors.timeout_accent;
      break;
    case tool_data::SubAgentStatus::Cancelled:
      icon = "-";
      accent = colors.running_accent;
      break;
    case tool_data::SubAgentStatus::Pending:
      icon = ".";
      accent = colors.pending_bg;
      break;
  }

  ftxui::Elements content;

  switch (call.status) {
    case tool_data::SubAgentStatus::Running:
      content.push_back(RenderWrappedLine(call.task, theme.chrome.body_text));
      if (call.tool_count > 0) {
        content.push_back(
            RenderWrappedLine("Running... " + std::to_string(call.tool_count) +
                                  " tool calls so far",
                              theme.chrome.dim_text));
      }
      break;
    case tool_data::SubAgentStatus::Complete:
      if (!call.result.empty()) {
        content.push_back(
            RenderWrappedLine(call.result, theme.chrome.body_text));
      }
      {
        std::string footer = std::to_string(call.tool_count) + " tool calls";
        if (call.elapsed_ms > 0) {
          footer += " \xc2\xb7 " + std::to_string(call.elapsed_ms / 1000) + "s";
        }
        content.push_back(RenderWrappedLine(footer, theme.chrome.dim_text));
      }
      break;
    case tool_data::SubAgentStatus::Error:
      content.push_back(RenderWrappedLine(call.result, colors.error_accent));
      break;
    case tool_data::SubAgentStatus::Timeout:
      content.push_back(RenderWrappedLine("Timed out", colors.timeout_accent));
      if (!call.result.empty()) {
        content.push_back(
            RenderWrappedLine(call.result, theme.chrome.body_text));
      }
      break;
    case tool_data::SubAgentStatus::Cancelled:
      content.push_back(RenderWrappedLine("Cancelled", theme.chrome.dim_text));
      break;
    case tool_data::SubAgentStatus::Pending:
      content.push_back(RenderWrappedLine(call.task, theme.chrome.body_text));
      content.push_back(
          RenderWrappedLine("Waiting to start...", theme.chrome.dim_text));
      break;
  }

  return RenderContainer(icon, "Sub-agent", accent, std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderTodoWrite(
    const tool_data::TodoWriteCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;

  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else if (call.todos.empty()) {
    content.push_back(ftxui::text("No todos") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    for (const auto& item : call.todos) {
      auto row = [&]() {
        if (item.status == "completed") {
          return ftxui::hbox({
              ftxui::text("[x]") | ftxui::color(theme.tool.edit_add),
              ftxui::text(" "),
              ftxui::text(item.content) |
                  ftxui::color(theme.semantic.text_body),
          });
        }
        if (item.status == "in_progress") {
          return ftxui::hbox({
              ftxui::text("[~]") | ftxui::color(theme.tool.bash_accent),
              ftxui::text(" "),
              ftxui::text(item.content) |
                  ftxui::color(theme.semantic.text_body),
          });
        }
        return ftxui::hbox({
                   ftxui::text("[ ]") | ftxui::color(theme.semantic.text_muted),
                   ftxui::text(" "),
                   ftxui::text(item.content) |
                       ftxui::color(theme.semantic.text_muted),
               }) |
               ftxui::dim;
      }();
      content.push_back(std::move(row));
    }
  }

  return RenderContainer("☑", "Todo List", theme.tool.edit_context,
                         std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderAskUser(
    const tool_data::AskUserCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;

  content.push_back(ftxui::paragraph(call.question) |
                    ftxui::color(theme.semantic.text_strong));

  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
    return RenderContainer("?", "ask_user", theme.tool.edit_remove,
                           std::move(content), theme);
  }

  if (!call.options.empty()) {
    content.push_back(ftxui::text(""));
    for (const auto& opt : call.options) {
      content.push_back(ftxui::hbox({
          ftxui::text("  \xe2\x80\xa2 ") |
              ftxui::color(theme.semantic.text_muted),
          ftxui::paragraph(opt) | ftxui::color(theme.semantic.text_body) |
              ftxui::flex,
      }));
    }
  }

  if (!call.response.empty()) {
    content.push_back(ftxui::text(""));
    content.push_back(ftxui::hbox({
        ftxui::text(std::string(layout::kCardPadX, ' ')),
        ftxui::paragraph(call.response) | ftxui::color(theme.dialog.input_fg) |
            ftxui::bgcolor(theme.dialog.input_bg) | ftxui::flex,
        ftxui::text(std::string(layout::kCardPadX, ' ')),
    }));
  }

  return RenderContainer("?", "ask_user", theme.semantic.accent_primary,
                         std::move(content), theme);
}

}  // namespace yac::presentation::tool_call
