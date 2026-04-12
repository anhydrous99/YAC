#include "message_renderer.hpp"

#include "theme.hpp"
#include "util/time_util.hpp"

#include <algorithm>
#include <utility>

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

namespace {

const char* ThinkingPulseGlyph(int frame) {
  switch (frame % 4) {
    case 0:
      return "\xC2\xB7";
    case 1:
    case 3:
      return "\xE2\x80\xA2";
    case 2:
    default:
      return "\xE2\x97\x8F";
  }
}

int MessageCardMaxWidth(const RenderContext& context) {
  constexpr int kHorizontalChromeWidth = 4;
  return std::max(1, context.terminal_width - kHorizontalChromeWidth);
}

}  // namespace

ftxui::Element MessageRenderer::Render(const Message& message,
                                       int current_width) {
  return Render(message, RenderContext{.terminal_width = current_width});
}

ftxui::Element MessageRenderer::Render(const Message& message,
                                       const RenderContext& context) {
  int current_width = context.terminal_width;
  const bool is_animated = message.sender == Sender::Agent &&
                           message.status == MessageStatus::Active;
  if (!is_animated && message.sender != Sender::Tool &&
      message.render_cache.element &&
      message.render_cache.terminal_width == current_width) {
    return *message.render_cache.element;
  }

  const ftxui::Element elem = SenderSwitch(
      message.sender, [&] { return RenderUserMessage(message, context); },
      [&] { return RenderAgentMessage(message, context); },
      [&] { return RenderToolCallMessage(message, context); },
      [] { return ftxui::text("Unknown Sender"); });

  if (is_animated || message.sender == Sender::Tool) {
    return elem;
  }

  message.render_cache.element = std::move(elem);
  message.render_cache.terminal_width = current_width;
  return *message.render_cache.element;
}

ftxui::Element MessageRenderer::RenderAll(const std::vector<Message>& messages,
                                          int current_width) {
  return RenderAll(messages, RenderContext{.terminal_width = current_width});
}

ftxui::Element MessageRenderer::RenderAll(const std::vector<Message>& messages,
                                          const RenderContext& context) {
  ftxui::Elements elements;
  for (const auto& message : messages) {
    elements.push_back(Render(message, context));
  }
  return ftxui::vbox(elements);
}

ftxui::Element MessageRenderer::RenderUserMessage(
    const Message& message, const RenderContext& context) {
  const auto& theme = context.Colors();
  auto content = ftxui::vbox({
      RenderHeader(Sender::User, message.DisplayLabel(), message.created_at,
                   message.render_cache.relative_time, context, message.status),
      ftxui::text(""),
      ftxui::hbox({ftxui::text("  "), ftxui::paragraph(message.Text()) |
                                          ftxui::color(theme.role.user) |
                                          ftxui::flex}),
  });

  auto styled_card = content | ftxui::bgcolor(theme.cards.user_bg) |
                     ftxui::borderRounded |
                     ftxui::color(theme.cards.user_border) |
                     ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN,
                                 MessageCardMaxWidth(context));

  return ftxui::hbox({ftxui::filler(), styled_card | ftxui::xflex_shrink});
}

ftxui::Element MessageRenderer::RenderAgentMessage(
    const Message& message, const RenderContext& context) {
  const auto& theme = context.Colors();
  const bool is_error = message.status == MessageStatus::Error;
  const bool is_active = message.status == MessageStatus::Active;
  const auto& blocks = message.render_cache.markdown_blocks.value_or(
      markdown::MarkdownParser::Parse(message.Text()));

  ftxui::Elements rows;
  rows.push_back(RenderHeader(
      Sender::Agent, message.DisplayLabel(), message.created_at,
      message.render_cache.relative_time, context, message.status));
  rows.push_back(ftxui::text(""));
  rows.push_back(markdown::MarkdownRenderer::Render(blocks, context));
  if (is_active && !message.Text().empty()) {
    rows.push_back(ftxui::hbox({
        ftxui::text("  "),
        ftxui::text("\xE2\x96\x8C") | ftxui::color(theme.role.agent) |
            ftxui::dim,
    }));
  }

  auto content = ftxui::vbox(std::move(rows));

  const auto& border_color =
      is_error ? theme.cards.error_border : theme.cards.agent_border;
  auto styled_card = content | ftxui::bgcolor(theme.cards.agent_bg) |
                     ftxui::borderRounded | ftxui::color(border_color) |
                     ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN,
                                 MessageCardMaxWidth(context));

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
  const auto& color = SenderSwitch(
      sender, [&]() -> const auto& { return theme.role.user; },
      [&]() -> const auto& {
        return is_error ? theme.role.error : theme.role.agent;
      },
      [&]() -> const auto& { return theme.tool.icon_fg; });

  char initial = label.empty() ? '?' : label[0];
  ftxui::Elements parts;
  parts.push_back(ftxui::text("[") | ftxui::color(color));
  parts.push_back(ftxui::text(std::string(1, initial)) | ftxui::bold |
                  ftxui::color(color));
  parts.push_back(ftxui::text("]") | ftxui::color(color));
  parts.push_back(ftxui::text(" "));
  parts.push_back(ftxui::text(label) | ftxui::bold | ftxui::color(color));

  if (is_active) {
    parts.push_back(ftxui::text(" \xC2\xB7 thinking ") |
                    ftxui::color(theme.chrome.dim_text));
    parts.push_back(ftxui::text(ThinkingPulseGlyph(context.thinking_frame)) |
                    ftxui::color(theme.role.agent) | ftxui::bold);
  }

  if (created_at != std::chrono::system_clock::time_point{}) {
    auto rel_time = util::FormatRelativeTime(created_at, cache);
    parts.push_back(ftxui::text(" \xC2\xB7 ") |
                    ftxui::color(theme.chrome.dim_text));
    parts.push_back(ftxui::text(rel_time) |
                    ftxui::color(theme.chrome.dim_text));
  }

  return ftxui::hbox(parts);
}

}  // namespace yac::presentation
