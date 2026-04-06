#include "chat_ui.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"

#include <utility>

namespace yac::presentation {

ChatUI::ChatUI() : on_send_([](const std::string&) {}) {}

ChatUI::ChatUI(OnSendCallback on_send) : on_send_(std::move(on_send)) {}

ftxui::Component ChatUI::Build() {
  auto message_list = BuildMessageList();
  auto input = BuildInput();

  // Only the input is interactive, so it receives focus immediately.
  return ftxui::Renderer(input, [this, message_list, input] {
    return ftxui::vbox({
               message_list->Render() | ftxui::flex,
               ftxui::separator(),
               ftxui::hbox({ftxui::text(" > "), input->Render() | ftxui::flex}),
           }) |
           ftxui::border;
  });
}

void ChatUI::AddMessage(Sender sender, std::string content) {
  messages_.push_back(Message{sender, std::move(content)});
}

const std::vector<Message>& ChatUI::GetMessages() const {
  return messages_;
}

void ChatUI::SubmitMessage() {
  if (input_content_.empty()) {
    return;
  }
  AddMessage(Sender::User, input_content_);
  std::string sent = input_content_;
  input_content_.clear();
  on_send_(sent);
}

ftxui::Component ChatUI::BuildInput() {
  ftxui::InputOption option;
  option.multiline = false;
  option.placeholder = "Type a message...";
  option.on_enter = [this] { SubmitMessage(); };
  return ftxui::Input(&input_content_, option);
}

ftxui::Component ChatUI::BuildMessageList() {
  return ftxui::Renderer([this] { return RenderMessages(); });
}

ftxui::Element ChatUI::RenderMessages() const {
  if (messages_.empty()) {
    return ftxui::vbox({ftxui::text("No messages yet.") | ftxui::dim}) |
           ftxui::flex;
  }

  return message_renderer_.RenderAll(messages_) | ftxui::yframe |
         ftxui::flex | ftxui::vscroll_indicator |
         ftxui::focusPositionRelative(0.F, 1.F);
}

}  // namespace yac::presentation
