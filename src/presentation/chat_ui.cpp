#include "chat_ui.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/dom/elements.hpp"
#include "theme.hpp"

#include <utility>

namespace yac::presentation {

inline const auto& k_theme = theme::Theme::Instance();

ChatUI::ChatUI() : on_send_([](const std::string&) {}) {}

ChatUI::ChatUI(OnSendCallback on_send) : on_send_(std::move(on_send)) {}

ftxui::Component ChatUI::Build() {
  auto message_list = BuildMessageList();
  auto input = BuildInput();

  return ftxui::Renderer(input, [this, message_list, input] {
    ftxui::Elements footer_elements;
    if (is_typing_) {
      footer_elements.push_back(ftxui::text("  ● Assistant is typing...") |
                                ftxui::color(k_theme.role.agent) | ftxui::bold);
    }
    if (!messages_.empty()) {
      auto count_label = "  [" + std::to_string(messages_.size()) + " message" +
                         (messages_.size() > 1 ? "s" : "") + "]";
      footer_elements.push_back(ftxui::filler());
      footer_elements.push_back(ftxui::text(count_label) |
                                ftxui::color(k_theme.chrome.dim_text) |
                                ftxui::dim);
    }

    auto input_area = ftxui::hbox({
        ftxui::text(" > ") | ftxui::color(k_theme.chrome.prompt) | ftxui::bold,
        input->Render() | ftxui::flex,
    });

    return ftxui::vbox({
               message_list->Render() | ftxui::flex,
               ftxui::separator() | ftxui::color(k_theme.markdown.separator),
               ftxui::hbox(footer_elements),
               ftxui::separator() | ftxui::color(k_theme.markdown.separator),
               input_area | ftxui::bgcolor(k_theme.cards.user_bg),
           }) |
           ftxui::borderRounded | ftxui::color(k_theme.chrome.border);
  });
}

void ChatUI::AddMessage(Sender sender, std::string content) {
  messages_.push_back(Message{sender, std::move(content)});
}

void ChatUI::SetTyping(bool typing) {
  is_typing_ = typing;
}

const std::vector<Message>& ChatUI::GetMessages() const {
  return messages_;
}

bool ChatUI::IsTyping() const {
  return is_typing_;
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
  if (messages_.empty() && !is_typing_) {
    return ftxui::vbox({ftxui::text("No messages yet.") | ftxui::dim |
                        ftxui::color(k_theme.chrome.dim_text)}) |
           ftxui::flex;
  }

  return MessageRenderer::RenderAll(messages_) | ftxui::yframe | ftxui::flex |
         ftxui::vscroll_indicator | ftxui::color(k_theme.chrome.dim_text) |
         ftxui::focusPositionRelative(0.F, 1.F);
}

}  // namespace yac::presentation
