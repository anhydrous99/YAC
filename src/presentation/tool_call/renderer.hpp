#pragma once

#include "../render_context.hpp"
#include "types.hpp"

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {

class ToolCallRenderer {
 public:
  ToolCallRenderer() = default;

  [[nodiscard]] static ftxui::Element Render(const ToolCallBlock& block);
  [[nodiscard]] static ftxui::Element Render(const ToolCallBlock& block,
                                             const RenderContext& context);

 private:
  [[nodiscard]] static ftxui::Element RenderBash(const BashCall& call,
                                                 const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderFileEdit(
      const FileEditCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderFileRead(
      const FileReadCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderGrep(const GrepCall& call,
                                                 const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderGlob(const GlobCall& call,
                                                 const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderWebFetch(
      const WebFetchCall& call, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderWebSearch(
      const WebSearchCall& call, const RenderContext& context);
};

}  // namespace yac::presentation::tool_call
