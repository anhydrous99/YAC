#pragma once

#include "ftxui/component/captured_mouse.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/screen/box.hpp"

#include <cstddef>

namespace yac::presentation {

class ChatUiScrollState {
 public:
  [[nodiscard]] ftxui::Box& VisibleBox();
  [[nodiscard]] ftxui::Box& ScrollbarBox();
  [[nodiscard]] int ScrollOffsetY() const;
  [[nodiscard]] int ContentHeight() const;
  [[nodiscard]] bool FollowTail() const;

  void SetContentHeight(int content_height);
  void ApplyMeasuredLayout();
  void OnMessagesChanged();
  [[nodiscard]] int PageLines() const;
  [[nodiscard]] int ViewportHeight() const;
  [[nodiscard]] int MaxScrollOffset() const;
  [[nodiscard]] size_t NewMessageCount(size_t message_count) const;
  [[nodiscard]] bool HandleEvent(ftxui::Event event, size_t message_count);
  void Clear();

 private:
  void ScrollUp(int lines, size_t message_count);
  void ScrollDown(int lines);
  void JumpHome();
  void JumpEnd();
  void ClampScrollOffset();
  [[nodiscard]] bool HandleMouseEvent(ftxui::Event event, size_t message_count);
  void UpdateScrollOffsetFromMouse(ftxui::Event event);

  int scroll_offset_y_ = 0;
  int content_height_ = 0;
  ftxui::Box visible_box_{};
  ftxui::Box scrollbar_box_{};
  bool scrollbar_dragging_ = false;
  bool follow_tail_ = true;
  size_t messages_seen_count_ = 0;
  ftxui::CapturedMouse captured_mouse_;
};

}  // namespace yac::presentation
