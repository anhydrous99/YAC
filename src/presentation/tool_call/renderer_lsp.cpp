#include "../util/count_summary.hpp"
#include "renderer.hpp"
#include "renderer_helpers.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {
namespace {

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

}  // namespace

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
    content.push_back(
        RenderWrappedLine(util::CountSummary(call.diagnostics.size(),
                                             "diagnostic", "diagnostics"),
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
        util::CountSummary(call.references.size(), "reference", "references"),
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
    content.push_back(
        RenderWrappedLine(util::CountSummary(call.definitions.size(),
                                             "definition", "definitions"),
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
    content.push_back(RenderWrappedLine(
        util::CountSummary(call.changes_count, "change", "changes"),
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
    content.push_back(RenderWrappedLine(
        util::CountSummary(call.symbols.size(), "symbol", "symbols"),
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

}  // namespace yac::presentation::tool_call
