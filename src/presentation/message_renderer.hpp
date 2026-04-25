#pragma once

#include "message.hpp"
#include "message_render_cache.hpp"
#include "render_context.hpp"

#include <string>
#include <vector>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

/// Renders Message objects into styled ftxui Elements.
///
/// `Render` and `RenderAll` produce static elements suitable for tests and
/// non-interactive paths. For the live UI, ChatUI composes the agent card
/// using the segment-level helpers (`RenderAgentHeader`, `RenderTextSegment`)
/// alongside interactive tool collapsibles.
class MessageRenderer {
 public:
  MessageRenderer() = default;

  [[nodiscard]] static ftxui::Element Render(const Message& message,
                                             int current_width);
  [[nodiscard]] static ftxui::Element Render(const Message& message,
                                             const RenderContext& context);
  [[nodiscard]] static ftxui::Element Render(const Message& message,
                                             MessageRenderCache& cache,
                                             const RenderContext& context);

  [[nodiscard]] static ftxui::Element RenderAll(
      const std::vector<Message>& messages, int current_width);
  [[nodiscard]] static ftxui::Element RenderAll(
      const std::vector<Message>& messages, const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderAll(
      const std::vector<Message>& messages,
      MessageRenderCacheStore& cache_store, const RenderContext& context);

  /// Renders the agent header (avatar, label, status, timestamp). Used as
  /// the first child of the agent card by both this renderer and ChatUI.
  [[nodiscard]] static ftxui::Element RenderAgentHeader(
      const Message& message, util::RelativeTimeCache& time_cache,
      const RenderContext& context);

  /// Renders a single text segment, parsing markdown with caching. Appends
  /// a streaming cursor when `is_streaming` is true.
  [[nodiscard]] static ftxui::Element RenderTextSegment(
      const std::string& text, bool is_streaming, TextSegmentCache& cache,
      const RenderContext& context);

  /// Wraps content in a card surface with padding and background color.
  [[nodiscard]] static ftxui::Element CardSurface(ftxui::Element content,
                                                  ftxui::Color background,
                                                  const RenderContext& context);

 private:
  [[nodiscard]] static ftxui::Element RenderUserMessage(
      const Message& message, MessageRenderCache& cache,
      const RenderContext& context);
  [[nodiscard]] static ftxui::Element RenderAgentMessage(
      const Message& message, MessageRenderCache& cache,
      const RenderContext& context);
};

}  // namespace yac::presentation
