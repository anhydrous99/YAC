#pragma once

#include "../render_context.hpp"
#include "tool_call/types.hpp"

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {

namespace tool_data = ::yac::tool_call;

class ToolCallRenderer {
 public:
  ToolCallRenderer() = default;

  [[nodiscard]] static ftxui::Element Render(
      const tool_data::ToolCallBlock& block);
  [[nodiscard]] static ftxui::Element Render(
      const tool_data::ToolCallBlock& block, const RenderContext& context);
  [[nodiscard]] static std::string BuildSummary(
      const tool_data::ToolCallBlock& block);

 private:
  [[nodiscard]] static ftxui::Element RenderBash(
      const tool_data::BashCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderFileEdit(
      const tool_data::FileEditCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderFileRead(
      const tool_data::FileReadCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderFileWrite(
      const tool_data::FileWriteCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderListDir(
      const tool_data::ListDirCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderGrep(
      const tool_data::GrepCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderGlob(
      const tool_data::GlobCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderWebFetch(
      const tool_data::WebFetchCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderWebSearch(
      const tool_data::WebSearchCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderLspDiagnostics(
      const tool_data::LspDiagnosticsCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderLspReferences(
      const tool_data::LspReferencesCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderLspGotoDefinition(
      const tool_data::LspGotoDefinitionCall& call,
      const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderLspRename(
      const tool_data::LspRenameCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderLspSymbols(
      const tool_data::LspSymbolsCall& call, const RenderContext& context);
};

}  // namespace yac::presentation::tool_call
