#pragma once

#include "chat_session.hpp"
#include "command_palette.hpp"
#include "composer_state.hpp"
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
  void AddToolCallMessage(::yac::presentation::tool_call::ToolCallBlock block);
  void SetCommands(std::vector<Command> commands);
  void SetTyping(bool typing);
  void SetToolExpanded(size_t index, bool expanded);

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
  void SyncMessageComponents();
  void ScrollUp(int lines);
  void ScrollDown(int lines);
  [[nodiscard]] int PageLines() const;
  [[nodiscard]] int ViewportHeight() const;
  [[nodiscard]] int MaxScrollOffset() const;
  void ClampScrollOffset();

  ChatSession session_;
  std::vector<ftxui::Component> message_components_;
  ComposerState composer_;
  OnSendCallback on_send_;
  bool is_typing_ = false;
  bool show_command_palette_ = false;
  std::vector<Command> commands_;

  int scroll_offset_y_ = 0;
  int content_height_ = 0;
  ftxui::Box visible_box_{};
  ftxui::Box scrollbar_box_{};
  bool scrollbar_dragging_ = false;
  bool follow_tail_ = true;
  ftxui::CapturedMouse captured_mouse_;
  mutable int last_terminal_width_ = -1;
};

}  // namespace yac::presentation
