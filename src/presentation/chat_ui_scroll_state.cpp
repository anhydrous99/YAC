#include "chat_ui_scroll_state.hpp"

#include "ftxui/component/app.hpp"
#include "util/scroll_math.hpp"

#include <algorithm>

namespace yac::presentation {

namespace {

bool IsHome(const ftxui::Event& event) {
  if (event.is_mouse() || event.input().empty()) {
    return false;
  }
  const auto& seq = event.input();
  return seq == "\x1b[H" || seq == "\x1b[1~" || seq == "\x1bOH";
}

bool IsEnd(const ftxui::Event& event) {
  if (event.is_mouse() || event.input().empty()) {
    return false;
  }
  const auto& seq = event.input();
  return seq == "\x1b[F" || seq == "\x1b[4~" || seq == "\x1bOF";
}

}  // namespace

ftxui::Box& ChatUiScrollState::VisibleBox() {
  return visible_box_;
}

ftxui::Box& ChatUiScrollState::ScrollbarBox() {
  return scrollbar_box_;
}

int ChatUiScrollState::ScrollOffsetY() const {
  return scroll_offset_y_;
}

int ChatUiScrollState::ContentHeight() const {
  return content_height_;
}

bool ChatUiScrollState::FollowTail() const {
  return follow_tail_;
}

void ChatUiScrollState::SetContentHeight(int content_height) {
  content_height_ = content_height;
}

void ChatUiScrollState::ApplyMeasuredLayout() {
  if (follow_tail_) {
    scroll_offset_y_ = MaxScrollOffset();
    return;
  }

  ClampScrollOffset();
}

void ChatUiScrollState::OnMessagesChanged(bool force_follow_tail) {
  if (force_follow_tail) {
    follow_tail_ = true;
  }
}

int ChatUiScrollState::PageLines() const {
  return ViewportHeight();
}

int ChatUiScrollState::ViewportHeight() const {
  int visible = visible_box_.y_max - visible_box_.y_min + 1;
  return std::max(1, visible);
}

int ChatUiScrollState::MaxScrollOffset() const {
  return util::CalculateMaxScrollOffset(content_height_, ViewportHeight());
}

size_t ChatUiScrollState::NewMessageCount(size_t message_count) const {
  return message_count - messages_seen_count_;
}

bool ChatUiScrollState::HandleEvent(ftxui::Event event, size_t message_count) {
  if (HandleMouseEvent(event, message_count)) {
    return true;
  }

  if (event == ftxui::Event::PageUp) {
    ScrollUp(PageLines(), message_count);
    return true;
  }
  if (event == ftxui::Event::PageDown) {
    ScrollDown(PageLines());
    return true;
  }
  if (IsHome(event)) {
    JumpHome();
    return true;
  }
  if (IsEnd(event)) {
    JumpEnd();
    return true;
  }
  return false;
}

void ChatUiScrollState::Clear() {
  scroll_offset_y_ = 0;
  content_height_ = 0;
  visible_box_ = {};
  scrollbar_box_ = {};
  scrollbar_dragging_ = false;
  follow_tail_ = true;
  messages_seen_count_ = 0;
  captured_mouse_ = nullptr;
}

void ChatUiScrollState::ScrollUp(int lines, size_t message_count) {
  scroll_offset_y_ = std::max(0, scroll_offset_y_ - lines);
  follow_tail_ = false;
  messages_seen_count_ = message_count;
}

void ChatUiScrollState::ScrollDown(int lines) {
  scroll_offset_y_ = std::min(scroll_offset_y_ + lines, MaxScrollOffset());
  follow_tail_ = scroll_offset_y_ >= MaxScrollOffset();
}

void ChatUiScrollState::JumpHome() {
  scroll_offset_y_ = 0;
  follow_tail_ = false;
}

void ChatUiScrollState::JumpEnd() {
  scroll_offset_y_ = MaxScrollOffset();
  follow_tail_ = true;
}

void ChatUiScrollState::ClampScrollOffset() {
  scroll_offset_y_ = util::ClampScrollOffset(scroll_offset_y_, content_height_,
                                             ViewportHeight());
  if (scroll_offset_y_ < MaxScrollOffset()) {
    follow_tail_ = false;
  }
}

bool ChatUiScrollState::HandleMouseEvent(ftxui::Event event,
                                         size_t message_count) {
  if (!event.is_mouse()) {
    return false;
  }

  if (captured_mouse_) {
    if (event.mouse().motion == ftxui::Mouse::Released) {
      captured_mouse_ = nullptr;
      scrollbar_dragging_ = false;
      return true;
    }
    UpdateScrollOffsetFromMouse(event);
    return true;
  }

  if (event.mouse().button == ftxui::Mouse::Left &&
      event.mouse().motion == ftxui::Mouse::Pressed &&
      scrollbar_box_.Contain(event.mouse().x, event.mouse().y)) {
    if (event.screen_ != nullptr) {
      captured_mouse_ = event.screen_->CaptureMouse();
    }
    if (captured_mouse_) {
      scrollbar_dragging_ = true;
      UpdateScrollOffsetFromMouse(event);
      return true;
    }
  }

  switch (event.mouse().button) {
    case ftxui::Mouse::WheelUp:
      ScrollUp(3, message_count);
      return true;
    case ftxui::Mouse::WheelDown:
      ScrollDown(3);
      return true;
    default:
      return false;
  }
}

void ChatUiScrollState::UpdateScrollOffsetFromMouse(ftxui::Event event) {
  int viewport_height = ViewportHeight();
  if (content_height_ <= 0 || viewport_height <= 0 ||
      scrollbar_box_.y_max < scrollbar_box_.y_min) {
    return;
  }

  int track_height = viewport_height;
  int thumb_size =
      util::CalculateThumbSize(content_height_, viewport_height, track_height);
  int track_usable = track_height - thumb_size;
  if (track_usable <= 0) {
    return;
  }

  int mouse_y = event.mouse().y - scrollbar_box_.y_min - (thumb_size / 2);
  float ratio = static_cast<float>(mouse_y) / static_cast<float>(track_usable);
  ratio = std::max(0.0F, std::min(1.0F, ratio));
  scroll_offset_y_ = util::CalculateScrollOffsetFromRatio(
      ratio, content_height_, viewport_height);
  follow_tail_ = scroll_offset_y_ >= MaxScrollOffset();
}

}  // namespace yac::presentation
