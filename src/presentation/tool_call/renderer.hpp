#pragma once

#include "types.hpp"

#include <ftxui/dom/elements.hpp>

namespace yac::presentation::tool_call {

class ToolCallRenderer {
 public:
  ToolCallRenderer() = default;

  [[nodiscard]] static ftxui::Element Render(const ToolCallBlock& block);

 private:
  [[nodiscard]] static ftxui::Element RenderBash(const BashCall& call);
  [[nodiscard]] static ftxui::Element RenderFileEdit(const FileEditCall& call);
  [[nodiscard]] static ftxui::Element RenderFileRead(const FileReadCall& call);
  [[nodiscard]] static ftxui::Element RenderGrep(const GrepCall& call);
  [[nodiscard]] static ftxui::Element RenderGlob(const GlobCall& call);
  [[nodiscard]] static ftxui::Element RenderWebFetch(const WebFetchCall& call);
  [[nodiscard]] static ftxui::Element RenderWebSearch(
      const WebSearchCall& call);
};

}  // namespace yac::presentation::tool_call
