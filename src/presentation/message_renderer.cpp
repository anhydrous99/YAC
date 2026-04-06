#include "message_renderer.hpp"

#include "theme.hpp"

#include <ftxui/dom/elements.hpp>

namespace yac::presentation {

MessageRenderer::MessageRenderer() = default;

ftxui::Element MessageRenderer::Render(const Message& message) const {
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
    const std::vector<Message>& messages) const {
  ftxui::Elements elements;
  for (const auto& msg : messages) {
    elements.push_back(Render(msg));
    elements.push_back(ftxui::text(""));  // Spacing between messages
  }
  return ftxui::vbox(elements);
}

ftxui::Element MessageRenderer::RenderUserMessage(const Message& message) {
  return ftxui::vbox({
      RenderHeader(Sender::User, message.DisplayLabel()),
      ftxui::paragraph(message.content) | ftxui::color(theme::KUserColor),
  });
}

ftxui::Element MessageRenderer::RenderAgentMessage(const Message& message) {
  auto blocks = markdown::MarkdownParser::Parse(message.content);
  return ftxui::vbox({
      RenderHeader(Sender::Agent, message.DisplayLabel()),
      markdown::MarkdownRenderer::Render(blocks),
  });
}

ftxui::Element MessageRenderer::RenderHeader(Sender sender,
                                             const std::string& label) {
  using namespace ftxui;

  const auto& color =
      (sender == Sender::User) ? theme::KUserColor : theme::KAgentColor;

  // Colored dot indicator + bold label
  return hbox({
      text("●") | bold | ftxui::color(color),
      text(" "),
      text(label) | bold | ftxui::color(color),
  });
}

}  // namespace yac::presentation
