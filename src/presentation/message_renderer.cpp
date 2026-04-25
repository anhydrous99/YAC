#include "message_renderer.hpp"

#include "markdown/parser.hpp"
#include "markdown/renderer.hpp"
#include "theme.hpp"
#include "tool_call/renderer.hpp"
#include "ui_spacing.hpp"
#include "util/time_util.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <string>
#include <utility>
#include <variant>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

namespace {

const char* ThinkingPulseGlyph(int frame) {
  constexpr std::array<const char*, 10> kBrailleFrames = {
      "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9", "\xe2\xa0\xb8",
      "\xe2\xa0\xbc", "\xe2\xa0\xb4", "\xe2\xa0\xa6", "\xe2\xa0\xa7",
      "\xe2\xa0\x87", "\xe2\xa0\x8f",
  };
  return kBrailleFrames.at(frame % kBrailleFrames.size());
}

int MessageCardMaxWidth(const RenderContext& context) {
  constexpr int kHorizontalBreathingRoom = 2;
  return std::max(1, context.terminal_width - kHorizontalBreathingRoom);
}

std::string StatusLabel(MessageStatus status, Sender sender) {
  switch (status) {
    case MessageStatus::Queued:
      return "queued";
    case MessageStatus::Active:
      return sender == Sender::Agent ? "thinking" : "sending";
    case MessageStatus::Cancelled:
      return "cancelled";
    case MessageStatus::Error:
      return "error";
    case MessageStatus::Complete:
      return {};
  }
  return {};
}

ftxui::Element RenderStatusLabel(MessageStatus status, Sender sender,
                                 const theme::Theme& theme) {
  const auto label = StatusLabel(status, sender);
  if (label.empty()) {
    return ftxui::emptyElement();
  }

  ftxui::Color color;
  switch (status) {
    case MessageStatus::Queued:
      color = theme.semantic.text_muted;
      break;
    case MessageStatus::Active:
      color = theme.semantic.accent_primary;
      break;
    case MessageStatus::Cancelled:
      color = theme.semantic.text_weak;
      break;
    case MessageStatus::Error:
      color = theme.role.error;
      break;
    default:
      return ftxui::emptyElement();
  }
  return ftxui::text(" \xC2\xB7 " + label + " ") | ftxui::color(color);
}

ftxui::Element RenderHeader(Sender sender, const std::string& label,
                            std::chrono::system_clock::time_point created_at,
                            util::RelativeTimeCache& cache,
                            const RenderContext& context,
                            MessageStatus status) {
  const auto& theme = context.Colors();
  const bool is_error = status == MessageStatus::Error;
  const bool is_active =
      sender == Sender::Agent && status == MessageStatus::Active;

  const auto& icon_color = SenderSwitch(
      sender, [&]() -> const auto& { return theme.role.user; },
      [&]() -> const auto& {
        return is_error ? theme.role.error : theme.role.agent;
      });

  const char* avatar = SenderSwitch(
      sender, [&]() -> const char* { return "\xe2\x97\x8f"; },
      [&]() -> const char* { return "\xe2\x97\x86"; });

  ftxui::Elements left_parts;
  left_parts.push_back(ftxui::text(avatar) | ftxui::color(icon_color));
  left_parts.push_back(ftxui::text(" "));
  left_parts.push_back(ftxui::text(label) |
                       ftxui::color(theme.semantic.text_weak));
  left_parts.push_back(RenderStatusLabel(status, sender, theme));

  if (is_active) {
    left_parts.push_back(
        ftxui::text(ThinkingPulseGlyph(context.thinking_frame)) |
        ftxui::color(theme.semantic.accent_primary) | ftxui::bold);
  }

  auto left = ftxui::hbox(std::move(left_parts));

  ftxui::Element right = ftxui::emptyElement();
  if (created_at != std::chrono::system_clock::time_point{}) {
    auto rel_time = util::FormatRelativeTime(created_at, cache);
    right = ftxui::text(" \xC2\xB7 " + rel_time) |
            ftxui::color(theme.semantic.text_muted);
  }

  return ftxui::hbox({left | ftxui::flex, right});
}

}  // namespace

ftxui::Element MessageRenderer::CardSurface(ftxui::Element content,
                                            ftxui::Color background,
                                            const RenderContext& context) {
  return ftxui::vbox({
             ftxui::text(""),
             ftxui::hbox({ftxui::text(std::string(layout::kCardPadX, ' ')),
                          std::move(content) | ftxui::flex,
                          ftxui::text(std::string(layout::kCardPadX, ' '))}),
             ftxui::text(""),
         }) |
         ftxui::bgcolor(background) |
         ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN,
                     MessageCardMaxWidth(context));
}

ftxui::Element MessageRenderer::RenderAgentHeader(
    const Message& message, util::RelativeTimeCache& time_cache,
    const RenderContext& context) {
  return RenderHeader(Sender::Agent, message.DisplayLabel(), message.created_at,
                      time_cache, context, message.status);
}

ftxui::Element MessageRenderer::RenderTextSegment(
    const std::string& text, bool is_streaming, TextSegmentCache& cache,
    const RenderContext& context) {
  if (cache.element.has_value() &&
      cache.terminal_width == context.terminal_width && !is_streaming) {
    return *cache.element;
  }

  if (!cache.markdown_blocks.has_value()) {
    cache.markdown_blocks = markdown::MarkdownParser::Parse(
        text, markdown::ParseOptions{.streaming = is_streaming});
  }

  ftxui::Element stream_cursor;
  if (is_streaming && !text.empty()) {
    stream_cursor = ftxui::text(" \xe2\x96\x8d") |
                    ftxui::color(context.Colors().semantic.accent_primary);
  }

  auto element = markdown::MarkdownRenderer::Render(
      *cache.markdown_blocks, context, std::move(stream_cursor));

  if (!is_streaming) {
    cache.element = element;
    cache.terminal_width = context.terminal_width;
  }
  return element;
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
  return SenderSwitch(
      message.sender,
      [&] { return RenderUserMessage(message, cache, context); },
      [&] { return RenderAgentMessage(message, cache, context); });
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
    if (!elements.empty()) {
      const int gap =
          (theme::CurrentTheme().density == theme::ThemeDensity::Compact)
              ? 1
              : layout::kSectionGap;
      for (int i = 0; i < gap; ++i) {
        elements.push_back(ftxui::text(" "));
      }
    }
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
  const bool time_cache_expired =
      cache.relative_time.has_value() &&
      std::chrono::system_clock::now() >= cache.relative_time->second;
  if (cache.element.has_value() &&
      cache.terminal_width == context.terminal_width && !time_cache_expired) {
    return *cache.element;
  }

  auto content = ftxui::vbox({
      RenderHeader(Sender::User, message.DisplayLabel(), message.created_at,
                   cache.relative_time, context, message.status),
      theme.density == theme::ThemeDensity::Compact ? ftxui::emptyElement()
                                                    : ftxui::text(""),
      ftxui::hbox({ftxui::text(std::string(layout::kCardPadX, ' ')),
                   ftxui::paragraph(message.CombinedText()) |
                       ftxui::color(theme.role.user) | ftxui::flex}),
      theme.density == theme::ThemeDensity::Compact ? ftxui::emptyElement()
                                                    : ftxui::text(""),
  });

  auto accent_rail =
      ftxui::text("▌") | ftxui::color(theme.semantic.accent_secondary);
  auto element =
      ftxui::hbox({accent_rail, std::move(content) | ftxui::flex |
                                    ftxui::bgcolor(theme.cards.user_bg)}) |
      ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, MessageCardMaxWidth(context));

  cache.element = element;
  cache.terminal_width = context.terminal_width;
  return element;
}

ftxui::Element MessageRenderer::RenderAgentMessage(
    const Message& message, MessageRenderCache& cache,
    const RenderContext& context) {
  const auto& theme = context.Colors();
  const bool is_active = message.status == MessageStatus::Active;

  ftxui::Elements rows;
  rows.push_back(RenderAgentHeader(message, cache.relative_time, context));
  if (theme.density != theme::ThemeDensity::Compact) {
    rows.push_back(ftxui::text(""));
  }

  // Find the index of the last text segment so we know where to put the
  // streaming cursor when the agent is still thinking.
  size_t last_text_segment = message.segments.size();
  for (size_t i = message.segments.size(); i-- > 0;) {
    if (std::holds_alternative<TextSegment>(message.segments[i])) {
      last_text_segment = i;
      break;
    }
  }

  size_t text_segment_idx = 0;
  bool first_segment = true;
  for (size_t i = 0; i < message.segments.size(); ++i) {
    if (!first_segment) {
      rows.push_back(ftxui::text(""));
    }
    first_segment = false;
    const auto& segment = message.segments[i];
    if (const auto* text = std::get_if<TextSegment>(&segment)) {
      const bool is_streaming = is_active && i == last_text_segment;
      auto& seg_cache = cache.EnsureSegment(text_segment_idx);
      rows.push_back(
          RenderTextSegment(text->text, is_streaming, seg_cache, context));
      ++text_segment_idx;
    } else if (const auto* tool = std::get_if<ToolSegment>(&segment)) {
      rows.push_back(tool_call::ToolCallRenderer::Render(tool->block, context));
    }
  }

  auto stack = ftxui::vbox(std::move(rows));
  return CardSurface(std::move(stack), theme.cards.agent_bg, context);
}

}  // namespace yac::presentation
