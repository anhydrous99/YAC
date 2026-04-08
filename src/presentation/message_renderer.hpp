#pragma once

#include "markdown/parser.hpp"
#include "markdown/renderer.hpp"
#include "message.hpp"

#include <chrono>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

/// Renders Message objects into styled ftxui Elements.
/// Handles the visual presentation of user vs agent messages,
/// with full markdown support for agent responses.
class MessageRenderer {
 public:
  MessageRenderer() = default;

  [[nodiscard]] static ftxui::Element Render(const Message& message);

  [[nodiscard]] static ftxui::Element RenderAll(
      const std::vector<Message>& messages);

 private:
  [[nodiscard]] static ftxui::Element RenderUserMessage(const Message& message);
  [[nodiscard]] static ftxui::Element RenderAgentMessage(
      const Message& message);
  [[nodiscard]] static ftxui::Element RenderHeader(
      Sender sender, const std::string& label,
      std::chrono::system_clock::time_point created_at = {});
};

}  // namespace yac::presentation
