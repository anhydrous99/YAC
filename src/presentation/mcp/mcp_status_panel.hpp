#pragma once

#include "ftxui/component/component.hpp"
#include "ftxui/dom/elements.hpp"
#include "mcp_status_sink.hpp"

#include <functional>

namespace yac::presentation {

class McpStatusPanel : public ftxui::ComponentBase {
 public:
  explicit McpStatusPanel(const McpStatusSink& sink);

  ftxui::Element OnRender() override;
  bool OnEvent(ftxui::Event event) override;

 private:
  const McpStatusSink& sink_;
  bool expanded_ = false;
  ftxui::Box header_box_;
};

ftxui::Component McpStatusPanelComponent(const McpStatusSink& sink);

}  // namespace yac::presentation
