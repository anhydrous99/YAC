#pragma once

#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/screen/box.hpp"
#include "message.hpp"
#include "message_renderer.hpp"

#include <functional>
#include <string>
#include <vector>

namespace yac::presentation {

class ChatUI {
 public:
  using OnSendCallback = std::function<void(const std::string&)>;

  static constexpr int kMaxInputLines = 8;

  ChatUI();
  explicit ChatUI(OnSendCallback on_send);

  [[nodiscard]] ftxui::Component Build();

  void AddMessage(Sender sender, std::string content);
  void SetTyping(bool typing);

  [[nodiscard]] const std::vector<Message>& GetMessages() const;
  [[nodiscard]] bool IsTyping() const;
  [[nodiscard]] int CalculateInputHeight() const;
  [[nodiscard]] bool HandleInputEvent(const ftxui::Event& event);

 private:
  void SubmitMessage();
  void InsertNewline();
  ftxui::Component BuildInput();
  ftxui::Component BuildMessageList();
  [[nodiscard]] ftxui::Element RenderMessages() const;
  void ScrollUp(int lines);
  void ScrollDown(int lines);
  [[nodiscard]] int PageLines() const;

  std::vector<Message> messages_;
  std::string input_content_;
  int input_cursor_ = 0;
  OnSendCallback on_send_;
  bool is_typing_ = false;

  int scroll_focus_y_ = 10000;
  int content_height_ = 0;
  ftxui::Box visible_box_{};
  ftxui::Box scrollbar_box_{};
  bool scrollbar_dragging_ = false;
  ftxui::CapturedMouse captured_mouse_;
  mutable int last_terminal_width_ = -1;
};

}  // namespace yac::presentation
