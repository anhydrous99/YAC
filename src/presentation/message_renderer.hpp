#pragma once

#include "markdown/parser.hpp"
#include "markdown/renderer.hpp"
#include "message.hpp"
#include "render_context.hpp"
#include "tool_call/renderer.hpp"

#include <chrono>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

/// Renders Message objects into styled ftxui Elements.
/// Handles the visual presentation of user vs agent messages,
/// with full markdown support for agent responses.
class MessageRenderer {
 public:
  MessageRenderer() = default;

  [[nodiscard]] static ftxui::Element Render(const Message& message,
                                             int current_width);
  [[nodiscard]] static ftxui::Element Render(const Message& message,
                                             const RenderContext& context);

  [[nodiscard]] static ftxui::Element RenderAll(
      const std::vector<Message>& messages, int current_width);
  [[nodiscard]] static ftxui::Element RenderAll(
      const std::vector<Message>& messages, const RenderContext& context);

 private:
  [[nodiscard]] static ftxui::Element RenderUserMessage(
      const Message& message, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderAgentMessage(
      const Message& message, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderToolCallMessage(
      const Message& message, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderHeader(
      Sender sender, const std::string& label,
      std::chrono::system_clock::time_point created_at,
      util::RelativeTimeCache& cache, const RenderContext& context,
      MessageStatus status = MessageStatus::Complete);
};

}  // namespace yac::presentation
