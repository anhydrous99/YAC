#include "message_renderer.hpp"

#include "theme.hpp"
#include "util/time_util.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

namespace {

const char* ThinkingPulseGlyph(int frame) {
  constexpr std::array<const char*, 10> kBrailleFrames = {
      "\xe2\xa0\x8b",  // ⠋
      "\xe2\xa0\x99",  // ⠙
      "\xe2\xa0\xb9",  // ⠹
      "\xe2\xa0\xb8",  // ⠸
      "\xe2\xa0\xbc",  // ⠼
      "\xe2\xa0\xb4",  // ⠴
      "\xe2\xa0\xa6",  // ⠦
      "\xe2\xa0\xa7",  // ⠧
      "\xe2\xa0\x87",  // ⠇
      "\xe2\xa0\x8f",  // ⠏
  };
  return kBrailleFrames.at(frame % kBrailleFrames.size());
}

int MessageCardMaxWidth(const RenderContext& context) {
  constexpr int kHorizontalBreathingRoom = 2;
  return std::max(1, context.terminal_width - kHorizontalBreathingRoom);
}

}  // namespace

ftxui::Element MessageRenderer::CardSurface(ftxui::Element content,
                                            ftxui::Color background,
                                            const RenderContext& context) {
  return ftxui::vbox({
             ftxui::text(""),
             ftxui::hbox({ftxui::text("  "), std::move(content) | ftxui::flex,
                          ftxui::text("  ")}),
             ftxui::text(""),
         }) |
         ftxui::bgcolor(background) |
         ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN,
                     MessageCardMaxWidth(context));
}

ftxui::Element MessageRenderer::Render(const Message& message,
                                       int current_width) {
  return Render(message, RenderContext{.terminal_width = current_width});
}

ftxui::Element MessageRenderer::Render(const Message& message,
                                       const RenderContext& context) {
  MessageRenderCache cache;
  return Render(message, cache, context);
}

ftxui::Element MessageRenderer::Render(const Message& message,
                                       MessageRenderCache& cache,
                                       const RenderContext& context) {
  int current_width = context.terminal_width;
  const bool is_animated = message.sender == Sender::Agent &&
                           message.status == MessageStatus::Active &&
                           message.Text().empty();
  if (!is_animated && message.sender != Sender::Tool && cache.element &&
      cache.terminal_width == current_width) {
    return *cache.element;
  }

  ftxui::Element elem = SenderSwitch(
      message.sender,
      [&] { return RenderUserMessage(message, cache, context); },
      [&] { return RenderAgentMessage(message, cache, context); },
      [&] { return RenderToolCallMessage(message, context); },
      [] { return ftxui::text("Unknown Sender"); });

  if (is_animated || message.sender == Sender::Tool) {
    return elem;
  }

  cache.element = std::move(elem);
  cache.terminal_width = current_width;
  return *cache.element;
}

ftxui::Element MessageRenderer::RenderAll(const std::vector<Message>& messages,
                                          int current_width) {
  return RenderAll(messages, RenderContext{.terminal_width = current_width});
}

ftxui::Element MessageRenderer::RenderAll(const std::vector<Message>& messages,
                                          const RenderContext& context) {
  MessageRenderCacheStore cache_store;
  return RenderAll(messages, cache_store, context);
}

ftxui::Element MessageRenderer::RenderAll(const std::vector<Message>& messages,
                                          MessageRenderCacheStore& cache_store,
                                          const RenderContext& context) {
  ftxui::Elements elements;
  for (const auto& message : messages) {
    if (message.id == 0) {
      MessageRenderCache cache;
      elements.push_back(Render(message, cache, context));
      continue;
    }
    elements.push_back(Render(message, cache_store.For(message.id), context));
  }
  return ftxui::vbox(elements);
}

ftxui::Element MessageRenderer::RenderUserMessage(
    const Message& message, MessageRenderCache& cache,
    const RenderContext& context) {
  const auto& theme = context.Colors();
  auto content = ftxui::vbox({
      RenderHeader(Sender::User, message.DisplayLabel(), message.created_at,
                   cache.relative_time, context, message.status),
      ftxui::text(""),
      ftxui::hbox({ftxui::text("  "), ftxui::paragraph(message.Text()) |
                                          ftxui::color(theme.role.user) |
                                          ftxui::flex}),
  });

  auto styled_card = MessageRenderer::CardSurface(std::move(content),
                                                  theme.cards.user_bg, context);

  return ftxui::hbox({ftxui::filler(), styled_card | ftxui::xflex_shrink});
}

ftxui::Element MessageRenderer::RenderAgentMessageContent(
    const Message& message, MessageRenderCache& cache,
    const RenderContext& context) {
  const auto& theme = context.Colors();
  const bool is_active = message.status == MessageStatus::Active;
  if (!cache.markdown_blocks.has_value()) {
    cache.markdown_blocks = markdown::MarkdownParser::Parse(
        message.Text(), markdown::ParseOptions{.streaming = is_active});
  }
  const auto& blocks = *cache.markdown_blocks;

  ftxui::Elements rows;
  rows.push_back(RenderHeader(Sender::Agent, message.DisplayLabel(),
                              message.created_at, cache.relative_time, context,
                              message.status));
  rows.push_back(ftxui::text(""));

  ftxui::Element stream_cursor;
  if (is_active && !message.Text().empty()) {
    stream_cursor = ftxui::text(" \xe2\x96\x8e") |
                    ftxui::color(theme.role.agent) | ftxui::dim;
  }
  rows.push_back(markdown::MarkdownRenderer::Render(blocks, context,
                                                    std::move(stream_cursor)));

  return ftxui::vbox(std::move(rows));
}

ftxui::Element MessageRenderer::RenderAgentMessage(
    const Message& message, MessageRenderCache& cache,
    const RenderContext& context) {
  const auto& theme = context.Colors();

  auto content = RenderAgentMessageContent(message, cache, context);

  auto styled_card = MessageRenderer::CardSurface(
      std::move(content), theme.cards.agent_bg, context);

  return ftxui::hbox({styled_card | ftxui::xflex_shrink, ftxui::filler()});
}

ftxui::Element MessageRenderer::RenderToolCallMessage(
    const Message& message, const RenderContext& context) {
  const auto* tool_call = message.ToolCall();
  if (tool_call == nullptr) {
    return ftxui::text("Tool call unavailable");
  }

  return tool_call::ToolCallRenderer::Render(*tool_call, context);
}

ftxui::Element MessageRenderer::RenderHeader(
    Sender sender, const std::string& label,
    std::chrono::system_clock::time_point created_at,
    util::RelativeTimeCache& cache, const RenderContext& context,
    MessageStatus status) {
  const auto& theme = context.Colors();
  const bool is_error =
      sender == Sender::Agent && status == MessageStatus::Error;
  const bool is_active =
      sender == Sender::Agent && status == MessageStatus::Active;
  const auto& icon_color = SenderSwitch(
      sender, [&]() -> const auto& { return theme.role.user; },
      [&]() -> const auto& {
        return is_error ? theme.role.error : theme.role.agent;
      },
      [&]() -> const auto& { return theme.tool.icon_fg; });

  const char* avatar = SenderSwitch(
      sender, [&]() -> const char* { return "\xe2\x97\x8f"; },
      [&]() -> const char* { return "\xe2\x97\x86"; },
      [&]() -> const char* { return "\xe2\x97\x8b"; });

  ftxui::Elements left_parts;
  left_parts.push_back(ftxui::text(avatar) | ftxui::bold |
                       ftxui::color(icon_color));
  left_parts.push_back(ftxui::text(" "));
  left_parts.push_back(ftxui::text(label) | ftxui::bold |
                       ftxui::color(icon_color));

  if (is_active) {
    left_parts.push_back(ftxui::text(" \xC2\xB7 thinking ") |
                         ftxui::color(theme.chrome.dim_text));
    left_parts.push_back(
        ftxui::text(ThinkingPulseGlyph(context.thinking_frame)) |
        ftxui::color(theme.role.agent) | ftxui::bold);
  }

  auto left = ftxui::hbox(std::move(left_parts));

  ftxui::Element right = ftxui::emptyElement();
  if (created_at != std::chrono::system_clock::time_point{}) {
    auto rel_time = util::FormatRelativeTime(created_at, cache);
    right = ftxui::text(" " + rel_time) | ftxui::color(theme.chrome.dim_text);
  }

  return ftxui::hbox({left | ftxui::flex, right});
}

}  // namespace yac::presentation
