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

  /// Build and return the root FTXUI component tree.
  ftxui::Component Build();

  /// Add a message to the display (e.g. an Agent response).
  void AddMessage(Sender sender, std::string content);

  /// Read-only access to the message history.
  [[nodiscard]] const std::vector<Message>& GetMessages() const;

 private:
  void SubmitMessage();
  ftxui::Component BuildInput();
  ftxui::Component BuildMessageList();
  [[nodiscard]] ftxui::Element RenderMessages() const;

  std::vector<Message> messages_;
  std::string input_content_;
  OnSendCallback on_send_;
  MessageRenderer message_renderer_;
};

}  // namespace yac::presentation
