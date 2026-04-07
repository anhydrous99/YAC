#include "message_renderer.hpp"

#include "theme.hpp"

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
      RenderHeader(Sender::User, message.DisplayLabel()),
      ftxui::text(""),
      ftxui::paragraph(message.content) | ftxui::color(k_theme.role.user),
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
      RenderHeader(Sender::Agent, message.DisplayLabel()),
      ftxui::text(""),
      markdown::MarkdownRenderer::Render(blocks),
  });

  return content | ftxui::bgcolor(k_theme.cards.agent_bg) |
         ftxui::borderRounded | ftxui::color(k_theme.cards.agent_border);
}

ftxui::Element MessageRenderer::RenderHeader(Sender sender,
                                             const std::string& label) {
  const auto& color =
      (sender == Sender::User) ? k_theme.role.user : k_theme.role.agent;

  return ftxui::hbox({
      ftxui::text("●") | ftxui::bold | ftxui::color(color),
      ftxui::text(" "),
      ftxui::text(label) | ftxui::bold | ftxui::color(color),
  });
}

}  // namespace yac::presentation
