#include "renderer.hpp"

#include "../theme.hpp"
#include "../ui_spacing.hpp"
#include "../util/string_util.hpp"
#include "descriptor.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {

namespace tool_data = ::yac::tool_call;

namespace {

constexpr size_t kMaxPreviewRows = 20;

ftxui::Element RenderCodeText(const std::string& text,
                              const theme::Theme& theme) {
  return ftxui::paragraph(text) | ftxui::bgcolor(theme.code.inline_bg) |
         ftxui::color(theme.code.inline_fg);
}

ftxui::Element RenderLabelValue(const std::string& label,
                                const std::string& value,
                                const theme::Theme& theme) {
  return ftxui::hbox(
      {ftxui::text(label) | ftxui::color(theme.semantic.text_muted),
       ftxui::paragraph(value) | ftxui::color(theme.semantic.text_body) |
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
      ftxui::text("\xe2\x96\x8c") | ftxui::color(accent),
      ftxui::text(" " + icon + " ") | ftxui::color(theme.semantic.text_muted),
      ftxui::text(label) | ftxui::color(theme.semantic.text_strong),
      ftxui::filler(),
  });

  auto body = ftxui::vbox(std::move(content)) |
              ftxui::color(theme.semantic.text_body) |
              ftxui::bgcolor(theme.semantic.surface_raised);

  return ftxui::vbox({
      header,
      ftxui::hbox({ftxui::text(std::string(layout::kCardPadX, ' ')),
                   body | ftxui::flex}),
  });
}

ftxui::Element RenderLines(const std::vector<std::string>& lines,
                           const theme::Theme& theme,
                           const std::string& empty_text = "") {
  if (lines.empty()) {
    return empty_text.empty() ? ftxui::text("")
                              : ftxui::text(empty_text) |
                                    ftxui::color(theme.semantic.text_muted);
  }

  ftxui::Elements elements;
  for (const auto& line : lines) {
    elements.push_back(RenderWrappedLine(line, theme.semantic.text_body));
  }
  return ftxui::vbox(std::move(elements));
}

void AddOmittedRows(ftxui::Elements& content, size_t total,
                    const theme::Theme& theme) {
  if (total <= kMaxPreviewRows) {
    return;
  }
  content.push_back(RenderWrappedLine(
      "... " + std::to_string(total - kMaxPreviewRows) + " more omitted",
      theme.semantic.text_muted));
}

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

std::string DiagnosticSeverityLabel(tool_data::DiagnosticSeverity severity) {
  switch (severity) {
    case tool_data::DiagnosticSeverity::Error:
      return "error";
    case tool_data::DiagnosticSeverity::Warning:
      return "warning";
    case tool_data::DiagnosticSeverity::Information:
      return "info";
    case tool_data::DiagnosticSeverity::Hint:
      return "hint";
  }
  return "info";
}

ftxui::Color DiagnosticSeverityColor(tool_data::DiagnosticSeverity severity,
                                     const theme::Theme& theme) {
  switch (severity) {
    case tool_data::DiagnosticSeverity::Error:
      return theme.tool.edit_remove;
    case tool_data::DiagnosticSeverity::Warning:
      return theme.tool.bash_accent;
    case tool_data::DiagnosticSeverity::Information:
      return theme.tool.read_accent;
    case tool_data::DiagnosticSeverity::Hint:
      return theme.semantic.text_muted;
  }
  return theme.semantic.text_muted;
}

ftxui::Element RenderError(const std::string& error,
                           const theme::Theme& theme) {
  return RenderWrappedLine("Error: " + error, theme.tool.edit_remove);
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
        } else if constexpr (std::is_same_v<T, tool_data::FileWriteCall>) {
          return RenderFileWrite(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::ListDirCall>) {
          return RenderListDir(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::GrepCall>) {
          return RenderGrep(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::GlobCall>) {
          return RenderGlob(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::WebFetchCall>) {
          return RenderWebFetch(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::WebSearchCall>) {
          return RenderWebSearch(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::LspDiagnosticsCall>) {
          return RenderLspDiagnostics(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::LspReferencesCall>) {
          return RenderLspReferences(call, context);
        } else if constexpr (std::is_same_v<T,
                                            tool_data::LspGotoDefinitionCall>) {
          return RenderLspGotoDefinition(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::LspRenameCall>) {
          return RenderLspRename(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::LspSymbolsCall>) {
          return RenderLspSymbols(call, context);
        } else if constexpr (std::is_same_v<T, tool_data::SubAgentCall>) {
          return RenderSubAgent(call, context);
        } else {
          return ftxui::text("");
        }
      },
      block);
}

std::string ToolCallRenderer::BuildSummary(
    const tool_data::ToolCallBlock& block) {
  return DescribeToolCall(block).summary;
}

std::string ToolCallRenderer::BuildGroupSummary(
    const std::vector<const tool_data::ToolCallBlock*>& blocks) {
  if (blocks.empty()) {
    return {};
  }

  struct Tally {
    std::string tag;
    int count;
  };
  std::vector<Tally> tallies;
  tallies.reserve(10);
  for (const auto* block : blocks) {
    if (block == nullptr) {
      continue;
    }
    auto tag = DescribeToolCall(*block).tag;
    auto it = std::find_if(tallies.begin(), tallies.end(),
                           [&tag](const Tally& t) { return t.tag == tag; });
    if (it == tallies.end()) {
      tallies.push_back({std::move(tag), 1});
    } else {
      it->count = it->count + 1;
    }
  }

  if (tallies.empty()) {
    return {};
  }

  std::sort(tallies.begin(), tallies.end(), [](const Tally& a, const Tally& b) {
    if (a.count != b.count) {
      return a.count > b.count;
    }
    return a.tag < b.tag;
  });

  constexpr size_t kMaxTerms = 4;
  const bool overflow = tallies.size() > kMaxTerms;
  const size_t limit = std::min(tallies.size(), kMaxTerms);

  std::string out;
  for (size_t i = 0; i < limit; ++i) {
    if (!out.empty()) {
      out += " \xc2\xb7 ";
    }
    out += std::to_string(tallies[i].count);
    out += ' ';
    out += tallies[i].tag;
  }
  if (overflow) {
    out += " \xc2\xb7 \xe2\x80\xa6";
  }
  return out;
}

ftxui::Element ToolCallRenderer::BuildWritePeek(
    const tool_data::FileWriteCall& call, const RenderContext& context) {
  if (call.content_tail.empty()) {
    return ftxui::Element{};
  }
  const auto& theme = context.Colors();
  const auto lines = util::SplitLines(call.content_tail);
  if (lines.empty()) {
    return ftxui::Element{};
  }
  const auto accent =
      call.is_error ? theme.tool.edit_remove : theme.tool.edit_add;

  auto make_bar_row = [&](const std::string& text) {
    return ftxui::hbox({
        ftxui::text(std::string(layout::kCardPadX, ' ')),
        ftxui::text("\xe2\x94\x83") | ftxui::color(accent) | ftxui::bold,
        ftxui::text(std::string(layout::kCardPadX, ' ')),
        ftxui::paragraph(text) | ftxui::color(theme.semantic.text_muted) |
            ftxui::dim | ftxui::flex,
        ftxui::text(std::string(layout::kCardPadX, ' ')),
    });
  };

  auto spacer_row = [&]() {
    return ftxui::hbox({
        ftxui::text(std::string(layout::kCardPadX, ' ')),
        ftxui::text("\xe2\x94\x83") | ftxui::color(accent),
        ftxui::filler(),
    });
  };

  const size_t limit = std::min(lines.size(), static_cast<size_t>(3));
  ftxui::Elements rows;
  rows.reserve(limit + 2);
  rows.push_back(spacer_row());
  for (size_t i = 0; i < limit; ++i) {
    rows.push_back(make_bar_row(lines[i]));
  }
  rows.push_back(spacer_row());
  return ftxui::vbox(std::move(rows));
}

std::string ToolCallRenderer::BuildLabel(
    const tool_data::ToolCallBlock& block) {
  return DescribeToolCall(block).label;
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
  content.push_back(ftxui::text(call.filepath) |
                    ftxui::color(theme.semantic.text_strong));

  if (call.diff.empty()) {
    content.push_back(ftxui::text("No diff lines") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    for (const auto& line : call.diff) {
      if (line.type == tool_data::DiffLine::Add) {
        content.push_back(ftxui::hbox({
            ftxui::text("+ ") | ftxui::color(theme.tool.edit_add),
            ftxui::paragraph(line.content) | ftxui::color(theme.tool.edit_add) |
                ftxui::flex,
        }));
      } else if (line.type == tool_data::DiffLine::Remove) {
        content.push_back(ftxui::hbox({
            ftxui::text("- ") | ftxui::color(theme.tool.edit_remove),
            ftxui::paragraph(line.content) |
                ftxui::color(theme.tool.edit_remove) | ftxui::flex,
        }));
      } else {
        content.push_back(ftxui::hbox({
            ftxui::text(std::string(layout::kCardPadX, ' ')) |
                ftxui::color(theme.tool.edit_context),
            ftxui::paragraph(line.content) |
                ftxui::color(theme.tool.edit_context) | ftxui::flex,
        }));
      }
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
  content.push_back(RenderWrappedLine(
      std::to_string(call.lines_loaded) + " lines", theme.semantic.text_muted));
  if (!call.excerpt.empty()) {
    content.push_back(
        RenderWrappedLine(call.excerpt, theme.semantic.text_body));
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
    if (call.is_streaming && lines.size() > kMaxPreviewRows) {
      const auto omitted = lines.size() - kMaxPreviewRows;
      content.push_back(
          ftxui::text("… " + std::to_string(omitted) + " earlier lines") |
          ftxui::color(theme.semantic.text_muted));
      for (auto index = lines.size() - kMaxPreviewRows; index < lines.size();
           ++index) {
        content.push_back(
            RenderWrappedLine(lines[index], theme.semantic.text_body));
      }
    } else {
      const auto limit = std::min(lines.size(), kMaxPreviewRows);
      for (size_t index = 0; index < limit; ++index) {
        content.push_back(
            RenderWrappedLine(lines[index], theme.semantic.text_body));
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
    content.push_back(
        RenderWrappedLine(std::to_string(call.entries.size()) + " entries",
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
  content.push_back(
      RenderWrappedLine(std::to_string(call.match_count) + " matches",
                        theme.semantic.text_muted));

  for (const auto& match : call.matches) {
    content.push_back(
        RenderWrappedLine(match.filepath + ":" + std::to_string(match.line),
                          theme.semantic.text_body));
    content.push_back(
        RenderWrappedLine(match.content, theme.semantic.text_muted));
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

  if (call.matched_files.empty()) {
    content.push_back(ftxui::text("No matches") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    content.push_back(RenderWrappedLine(
        std::to_string(call.matched_files.size()) + " matches",
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

ftxui::Element ToolCallRenderer::RenderLspDiagnostics(
    const tool_data::LspDiagnosticsCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.file_path) |
                    ftxui::color(theme.semantic.text_strong));
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else if (call.diagnostics.empty()) {
    content.push_back(ftxui::text("No diagnostics") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    content.push_back(RenderWrappedLine(
        std::to_string(call.diagnostics.size()) + " diagnostics",
        theme.semantic.text_muted));
    const auto limit = std::min(call.diagnostics.size(), kMaxPreviewRows);
    for (size_t index = 0; index < limit; ++index) {
      const auto& diag = call.diagnostics[index];
      content.push_back(ftxui::hbox({
          ftxui::text("\xe2\x97\x8f") |
              ftxui::color(DiagnosticSeverityColor(diag.severity, theme)),
          ftxui::text(" "),
          ftxui::paragraph(std::to_string(diag.line) + " " +
                           DiagnosticSeverityLabel(diag.severity) + ": " +
                           diag.message) |
              ftxui::color(theme.semantic.text_body) | ftxui::flex,
      }));
    }
    AddOmittedRows(content, call.diagnostics.size(), theme);
  }

  const auto accent =
      call.is_error ? theme.tool.edit_remove : theme.tool.read_accent;
  return RenderContainer("λ", "diagnostics", accent, std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderLspReferences(
    const tool_data::LspReferencesCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.file_path) |
                    ftxui::color(theme.semantic.text_strong));
  if (!call.symbol.empty()) {
    content.push_back(
        RenderWrappedLine("Symbol: " + call.symbol, theme.semantic.text_muted));
  }
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else if (call.references.empty()) {
    content.push_back(ftxui::text("No references") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    content.push_back(RenderWrappedLine(
        std::to_string(call.references.size()) + " references",
        theme.semantic.text_muted));
    const auto limit = std::min(call.references.size(), kMaxPreviewRows);
    for (size_t index = 0; index < limit; ++index) {
      const auto& ref = call.references[index];
      content.push_back(RenderWrappedLine(ref.filepath + ":" +
                                              std::to_string(ref.line) + ":" +
                                              std::to_string(ref.character),
                                          theme.semantic.text_body));
    }
    AddOmittedRows(content, call.references.size(), theme);
  }

  const auto accent =
      call.is_error ? theme.tool.edit_remove : theme.tool.grep_accent;
  return RenderContainer("⌕", "references", accent, std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderLspGotoDefinition(
    const tool_data::LspGotoDefinitionCall& call,
    const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.file_path) |
                    ftxui::color(theme.semantic.text_strong));
  content.push_back(RenderWrappedLine("Position: " + std::to_string(call.line) +
                                          ":" + std::to_string(call.character),
                                      theme.semantic.text_muted));
  if (!call.symbol.empty()) {
    content.push_back(
        RenderWrappedLine("Symbol: " + call.symbol, theme.semantic.text_muted));
  }
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else if (call.definitions.empty()) {
    content.push_back(ftxui::text("No definitions") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    content.push_back(RenderWrappedLine(
        std::to_string(call.definitions.size()) + " definitions",
        theme.semantic.text_muted));
    const auto limit = std::min(call.definitions.size(), kMaxPreviewRows);
    for (size_t index = 0; index < limit; ++index) {
      const auto& def = call.definitions[index];
      content.push_back(RenderWrappedLine(def.filepath + ":" +
                                              std::to_string(def.line) + ":" +
                                              std::to_string(def.character),
                                          theme.semantic.text_body));
    }
    AddOmittedRows(content, call.definitions.size(), theme);
  }

  const auto accent =
      call.is_error ? theme.tool.edit_remove : theme.tool.read_accent;
  return RenderContainer("↦", "definition", accent, std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderLspRename(
    const tool_data::LspRenameCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.file_path) |
                    ftxui::color(theme.semantic.text_strong));
  content.push_back(RenderWrappedLine("Position: " + std::to_string(call.line) +
                                          ":" + std::to_string(call.character),
                                      theme.semantic.text_muted));
  if (!call.old_name.empty()) {
    content.push_back(
        RenderWrappedLine("Old: " + call.old_name, theme.semantic.text_muted));
  }
  content.push_back(
      RenderWrappedLine("New: " + call.new_name, theme.semantic.text_muted));
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else {
    content.push_back(
        RenderWrappedLine(std::to_string(call.changes_count) + " changes",
                          theme.semantic.text_muted));
    const auto limit = std::min(call.changes.size(), kMaxPreviewRows);
    for (size_t index = 0; index < limit; ++index) {
      const auto& edit = call.changes[index];
      content.push_back(RenderWrappedLine(
          edit.filepath + ":" + std::to_string(edit.start_line) + ":" +
              std::to_string(edit.start_character),
          theme.semantic.text_body));
    }
    AddOmittedRows(content, call.changes.size(), theme);
  }

  const auto accent =
      call.is_error ? theme.tool.edit_remove : theme.tool.edit_context;
  return RenderContainer("↺", "rename", accent, std::move(content), theme);
}

ftxui::Element ToolCallRenderer::RenderLspSymbols(
    const tool_data::LspSymbolsCall& call, const RenderContext& context) {
  const auto& theme = context.Colors();
  ftxui::Elements content;
  content.push_back(ftxui::text(call.file_path) |
                    ftxui::color(theme.semantic.text_strong));
  if (call.is_error) {
    content.push_back(RenderError(call.error, theme));
  } else if (call.symbols.empty()) {
    content.push_back(ftxui::text("No symbols") |
                      ftxui::color(theme.semantic.text_muted));
  } else {
    content.push_back(
        RenderWrappedLine(std::to_string(call.symbols.size()) + " symbols",
                          theme.semantic.text_muted));
    const auto limit = std::min(call.symbols.size(), kMaxPreviewRows);
    for (size_t index = 0; index < limit; ++index) {
      const auto& symbol = call.symbols[index];
      content.push_back(RenderWrappedLine(
          std::to_string(symbol.line) + " " + symbol.kind + " " + symbol.name,
          theme.semantic.text_body));
    }
    AddOmittedRows(content, call.symbols.size(), theme);
  }

  const auto accent =
      call.is_error ? theme.tool.edit_remove : theme.tool.glob_accent;
  return RenderContainer("◇", "symbols", accent, std::move(content), theme);
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

}  // namespace yac::presentation::tool_call
