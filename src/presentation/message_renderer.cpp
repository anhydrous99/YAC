#include "message_renderer.hpp"

#include "theme.hpp"

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

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
  for (size_t i = 0; i < messages.size(); ++i) {
    elements.push_back(Render(messages[i]));
    if (i + 1 < messages.size()) {
      elements.push_back(ftxui::text(""));  // Spacing between messages
    }
  }
  return ftxui::vbox(elements);
}

ftxui::Element MessageRenderer::RenderUserMessage(const Message& message) {
  auto card = ftxui::vbox({
      RenderHeader(Sender::User, message.DisplayLabel()),
      ftxui::text(""),
      ftxui::paragraph(message.content) | ftxui::color(theme::KUserColor),
  });

  auto styled_card = card | ftxui::bgcolor(theme::KUserCardBg) |
                     ftxui::borderRounded |
                     ftxui::color(theme::KUserCardBorder);

  return ftxui::hbox({ftxui::filler(), styled_card});
}

ftxui::Element MessageRenderer::RenderAgentMessage(const Message& message) {
  auto blocks = markdown::MarkdownParser::Parse(message.content);
  auto card = ftxui::vbox({
      RenderHeader(Sender::Agent, message.DisplayLabel()),
      ftxui::text(""),
      markdown::MarkdownRenderer::Render(blocks),
  });

  return card | ftxui::bgcolor(theme::KAgentCardBg) | ftxui::borderRounded |
         ftxui::color(theme::KAgentCardBorder);
}

ftxui::Element MessageRenderer::RenderHeader(Sender sender,
                                             const std::string& label) {
  using namespace ftxui;

  const auto& color =
      (sender == Sender::User) ? theme::KUserColor : theme::KAgentColor;

  return hbox({
      text("●") | bold | ftxui::color(color),
      text(" "),
      text(label) | bold | ftxui::color(color),
  });
}

}  // namespace yac::presentation
