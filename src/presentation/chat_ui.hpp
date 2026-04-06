#pragma once

#include "ftxui/component/component.hpp"
#include "message.hpp"
#include "message_renderer.hpp"

#include <functional>
#include <string>
#include <vector>

namespace yac::presentation {

class ChatUI {
 public:
  using OnSendCallback = std::function<void(const std::string&)>;

  ChatUI();
  explicit ChatUI(OnSendCallback on_send);

  [[nodiscard]] ftxui::Component Build();

  void AddMessage(Sender sender, std::string content);
  void SetTyping(bool typing);

  [[nodiscard]] const std::vector<Message>& GetMessages() const;
  [[nodiscard]] bool IsTyping() const;

 private:
  void SubmitMessage();
  ftxui::Component BuildInput();
  ftxui::Component BuildMessageList();
  [[nodiscard]] ftxui::Element RenderMessages() const;

  std::vector<Message> messages_;
  std::string input_content_;
  OnSendCallback on_send_;
  bool is_typing_ = false;
};

}  // namespace yac::presentation
