#include "message_renderer.hpp"

#include "theme.hpp"
#include "util/time_util.hpp"

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

inline const auto& k_theme = theme::Theme::Instance();

ftxui::Element MessageRenderer::Render(const Message& message) {
  switch (message.sender) {
    case Sender::User:
      return RenderUserMessage(message);
    case Sender::Agent:
      return RenderAgentMessage(message);
    default:
      return ftxui::text("Unknown sender");
  }
}

ftxui::Element MessageRenderer::RenderAll(
    const std::vector<Message>& messages) {
  ftxui::Elements elements;
  for (const auto& message : messages) {
    elements.push_back(Render(message));
  }
  return ftxui::vbox(elements);
}

ftxui::Element MessageRenderer::RenderUserMessage(const Message& message) {
  auto content = ftxui::vbox({
      RenderHeader(Sender::User, message.DisplayLabel(), message.created_at),
      ftxui::text(""),
      ftxui::hbox({
          ftxui::text("  "),
          ftxui::paragraph(message.content) | ftxui::color(k_theme.role.user) |
              ftxui::flex,
      }),
  });

  auto styled_card = content | ftxui::bgcolor(k_theme.cards.user_bg) |
                     ftxui::borderRounded |
                     ftxui::color(k_theme.cards.user_border) |
                     ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 80);

  return ftxui::hbox({ftxui::filler(), styled_card});
}

ftxui::Element MessageRenderer::RenderAgentMessage(const Message& message) {
  const auto& blocks = message.cached_blocks.value_or(
      markdown::MarkdownParser::Parse(message.content));
  auto content = ftxui::vbox({
      RenderHeader(Sender::Agent, message.DisplayLabel(), message.created_at),
      ftxui::text(""),
      markdown::MarkdownRenderer::Render(blocks),
  });

  return content | ftxui::bgcolor(k_theme.cards.agent_bg) |
         ftxui::borderRounded | ftxui::color(k_theme.cards.agent_border);
}

ftxui::Element MessageRenderer::RenderHeader(
    Sender sender, const std::string& label,
    std::chrono::system_clock::time_point created_at) {
  const auto& color =
      (sender == Sender::User) ? k_theme.role.user : k_theme.role.agent;

  char initial = label.empty() ? '?' : label[0];
  ftxui::Elements parts;
  parts.push_back(ftxui::text("[") | ftxui::color(color));
  parts.push_back(ftxui::text(std::string(1, initial)) | ftxui::bold |
                  ftxui::color(color));
  parts.push_back(ftxui::text("]") | ftxui::color(color));
  parts.push_back(ftxui::text(" "));
  parts.push_back(ftxui::text(label) | ftxui::bold | ftxui::color(color));

  if (created_at != std::chrono::system_clock::time_point{}) {
    auto rel_time = util::FormatRelativeTime(created_at);
    parts.push_back(ftxui::text(" \xC2\xB7 ") |
                    ftxui::color(k_theme.chrome.dim_text));
    parts.push_back(ftxui::text(rel_time) |
                    ftxui::color(k_theme.chrome.dim_text));
  }

  return ftxui::hbox(parts);
}

}  // namespace yac::presentation
