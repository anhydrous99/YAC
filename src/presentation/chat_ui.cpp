#include "chat_ui.hpp"

#include "collapsible.hpp"
#include "command_palette.hpp"
#include "dialog.hpp"
#include "ftxui/component/animation.hpp"
#include "ftxui/component/app.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "theme.hpp"
#include "util/scroll_math.hpp"

#include <algorithm>
#include <chrono>
#include <functional>
#include <utility>

namespace yac::presentation {

inline const auto& k_theme = theme::Theme::Instance();

namespace {

class DynamicMessageStack : public ftxui::ComponentBase {
 public:
  explicit DynamicMessageStack(std::function<ftxui::Components()> get_children)
      : get_children_(std::move(get_children)) {}

  ftxui::Element OnRender() override {
    SyncChildren();

    ftxui::Elements elements;
    for (const auto& child : children_) {
      elements.push_back(child->Render());
    }
    return ftxui::vbox(std::move(elements));
  }

  bool OnEvent(ftxui::Event event) override {
    SyncChildren();
    return ComponentBase::OnEvent(event);
  }

 private:
  void SyncChildren() {
    auto children = get_children_();
    DetachAllChildren();
    for (auto& child : children) {
      Add(child);
    }
  }

  std::function<ftxui::Components()> get_children_;
};

class ThinkingAnimationDriver : public ftxui::ComponentBase {
 public:
  ThinkingAnimationDriver(ftxui::Component child,
                          std::function<bool()> is_running,
                          std::function<void()> advance_frame)
      : is_running_(std::move(is_running)),
        advance_frame_(std::move(advance_frame)) {
    Add(std::move(child));
  }

  ftxui::Element OnRender() override {
    if (is_running_()) {
      ftxui::animation::RequestAnimationFrame();
    }
    return children_.front()->Render();
  }

  void OnAnimation(ftxui::animation::Params& params) override {
    ComponentBase::OnAnimation(params);

    if (!is_running_()) {
      elapsed_ = ftxui::animation::Duration::zero();
      return;
    }

    elapsed_ += params.duration();
    const ftxui::animation::Duration frame_duration =
        std::chrono::milliseconds(260);
    while (elapsed_ >= frame_duration) {
      advance_frame_();
      elapsed_ -= frame_duration;
    }
    ftxui::animation::RequestAnimationFrame();
  }

 private:
  std::function<bool()> is_running_;
  std::function<void()> advance_frame_;
  ftxui::animation::Duration elapsed_{};
};

bool IsAltEnter(const ftxui::Event& event) {
  if (event.is_mouse() || event.input().empty()) {
    return false;
  }
  const auto& seq = event.input();
  return seq == "\x1b\r" || seq == "\x1b\n";
}

bool IsShiftEnter(const ftxui::Event& event) {
  if (event.is_mouse() || event.input().empty()) {
    return false;
  }
  const auto& seq = event.input();
  return seq == "\x1b[13;2~" || seq == "\x1b[27;2;13~" || seq == "\x1b[13;2u";
}

bool IsCtrlEnter(const ftxui::Event& event) {
  if (event.is_mouse() || event.input().empty()) {
    return false;
  }
  const auto& seq = event.input();
  return seq == "\x1b[13;5~" || seq == "\x1b[27;5;13~" || seq == "\x1b[13;5u";
}

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

ChatUI::ChatUI() : on_send_([](const std::string&) {}) {}

ChatUI::ChatUI(OnSendCallback on_send) : on_send_(std::move(on_send)) {}

void ChatUI::SetOnSend(OnSendCallback on_send) {
  on_send_ = std::move(on_send);
}

void ChatUI::SetOnCommand(OnCommandCallback on_command) {
  on_command_ = std::move(on_command);
}

ftxui::Component ChatUI::Build() {
  auto message_list = BuildMessageList();
  auto input = BuildInput();

  auto container = ftxui::Container::Stacked({message_list, input});

  auto main_ui = ftxui::Renderer(container, [this, message_list, input] {
    ftxui::Elements footer_elements;
    const bool has_active_agent = HasActiveAgentMessage();
    if (is_typing_ && !has_active_agent) {
      footer_elements.push_back(ftxui::text("  ● Assistant is typing...") |
                                ftxui::color(k_theme.role.agent) | ftxui::bold);
    }
    if (!session_.Empty()) {
      auto count_label = "  [" + std::to_string(session_.MessageCount()) +
                         " message" + (session_.MessageCount() > 1 ? "s" : "") +
                         "]";
      footer_elements.push_back(ftxui::filler());
      footer_elements.push_back(ftxui::text(count_label) |
                                ftxui::color(k_theme.chrome.dim_text) |
                                ftxui::dim);
    }

    auto input_area = ftxui::hbox({
        ftxui::text(" > ") | ftxui::color(k_theme.chrome.prompt) | ftxui::bold,
        input->Render() | ftxui::flex,
    });

    auto input_height = CalculateInputHeight();

    ftxui::Elements footer_rows;
    footer_rows.push_back(ftxui::hbox(footer_elements));

    footer_rows.push_back(
        ftxui::text(" Enter=Send │ Ctrl+P=Commands │ ⇧+Enter=Newline │ "
                    "PgUp/PgDn=Scroll │ Home/End") |
        ftxui::color(k_theme.chrome.dim_text) | ftxui::dim);

    auto footer_with_hints = ftxui::vbox(footer_rows);

    return ftxui::vbox({
               message_list->Render() | ftxui::flex,
               ftxui::separator() | ftxui::color(k_theme.markdown.separator),
               footer_with_hints,
               ftxui::separator() | ftxui::color(k_theme.markdown.separator),
               input_area | ftxui::bgcolor(k_theme.cards.user_bg) |
                   ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN,
                               input_height),
           }) |
           ftxui::borderRounded | ftxui::color(k_theme.chrome.border);
  });

  auto on_select = [this](int index) {
    if (index >= 0 && static_cast<size_t>(index) < commands_.size() &&
        on_command_) {
      on_command_(commands_[index].name);
    }
  };
  auto palette = CommandPalette(commands_, on_select, &show_command_palette_);
  auto dialog = DialogPanel("Command Palette", palette, &show_command_palette_);
  auto modal = ftxui::Modal(main_ui, dialog, &show_command_palette_);
  auto animated_modal = ftxui::Make<ThinkingAnimationDriver>(
      modal, [this] { return HasActiveAgentMessage(); },
      [this] { AdvanceThinkingFrame(); });

  return ftxui::CatchEvent(animated_modal, [this](const ftxui::Event& event) {
    if (event.input() == "\x10") {
      show_command_palette_ = true;
      return true;
    }
    return false;
  });
}

MessageId ChatUI::AddMessage(Sender sender, std::string content,
                             MessageStatus status) {
  auto id = session_.AddMessage(sender, std::move(content), status);
  if (sender == Sender::Agent && status == MessageStatus::Active) {
    thinking_frame_ = 0;
  }
  if (!scrollbar_dragging_) {
    follow_tail_ = true;
  }
  return id;
}

MessageId ChatUI::StartAgentMessage() {
  auto id = session_.AddMessage(Sender::Agent, "", MessageStatus::Active);
  thinking_frame_ = 0;
  if (!scrollbar_dragging_) {
    follow_tail_ = true;
  }
  SyncMessageComponents();
  return id;
}

void ChatUI::AppendToAgentMessage(MessageId id, std::string delta) {
  session_.AppendToAgentMessage(id, std::move(delta));
  if (!scrollbar_dragging_) {
    follow_tail_ = true;
  }
  SyncMessageComponents();
}

void ChatUI::SetMessageStatus(MessageId id, MessageStatus status) {
  if (status == MessageStatus::Active) {
    thinking_frame_ = 0;
  }
  session_.SetMessageStatus(id, status);
}

void ChatUI::AddToolCallMessage(
    ::yac::presentation::tool_call::ToolCallBlock block) {
  session_.AddToolCallMessage(std::move(block));
  if (!scrollbar_dragging_) {
    follow_tail_ = true;
  }
}

void ChatUI::SetCommands(std::vector<Command> commands) {
  commands_ = std::move(commands);
}

void ChatUI::SetSlashCommands(SlashCommandRegistry registry) {
  slash_commands_ = std::move(registry);
}

void ChatUI::SetTyping(bool typing) {
  is_typing_ = typing;
}

void ChatUI::SetToolExpanded(size_t index, bool expanded) {
  session_.SetToolExpanded(index, expanded);
}

void ChatUI::ClearMessages() {
  session_.ClearMessages();
  message_components_.clear();
}

const std::vector<Message>& ChatUI::GetMessages() const {
  return session_.Messages();
}

bool ChatUI::IsTyping() const {
  return is_typing_;
}

void ChatUI::SubmitMessage() {
  if (composer_.Empty()) {
    return;
  }
  std::string sent = composer_.Submit();
  if (slash_commands_.TryDispatch(sent)) {
    return;
  }
  AddMessage(Sender::User, sent);
  on_send_(sent);
}

ftxui::Component ChatUI::BuildInput() {
  ftxui::InputOption option;
  option.multiline = true;
  option.placeholder = "Type a message...";
  option.cursor_position = composer_.CursorPosition();
  option.transform = [](ftxui::InputState state) {
    state.element |= ftxui::color(k_theme.chrome.body_text);
    if (state.is_placeholder) {
      state.element |= ftxui::dim | ftxui::color(k_theme.chrome.dim_text);
    }
    if (state.focused) {
      state.element |= ftxui::focusCursorBarBlinking;
    }
    return state.element;
  };

  auto input = ftxui::Input(&composer_.Content(), option);

  return ftxui::CatchEvent(input, [this](const ftxui::Event& event) {
    return HandleInputEvent(event);
  });
}

int ChatUI::CalculateInputHeight() const {
  return composer_.CalculateHeight(kMaxInputLines);
}

void ChatUI::InsertNewline() {
  composer_.InsertNewline();
}

bool ChatUI::HandleInputEvent(const ftxui::Event& event) {
  if (event == ftxui::Event::Return) {
    SubmitMessage();
    return true;
  }
  if (IsAltEnter(event) || IsShiftEnter(event) || IsCtrlEnter(event)) {
    InsertNewline();
    return true;
  }
  return false;
}

ftxui::Component ChatUI::BuildMessageList() {
  SyncMessageComponents();
  auto message_stack = ftxui::Make<DynamicMessageStack>([this] {
    SyncMessageComponents();
    return message_components_;
  });

  auto content = ftxui::Renderer(message_stack, [this, message_stack] {
    SyncMessageComponents();
    auto msg_element =
        message_stack->Render() | ftxui::color(k_theme.chrome.dim_text);

    msg_element->ComputeRequirement();
    content_height_ = msg_element->requirement().min_y;
    int viewport_height = ViewportHeight();
    if (follow_tail_) {
      scroll_offset_y_ = MaxScrollOffset();
    } else {
      ClampScrollOffset();
    }

    ftxui::Element focused_messages;
    if (follow_tail_) {
      focused_messages = msg_element | ftxui::focusPositionRelative(0.0F, 1.0F);
    } else {
      int focus_y =
          util::CalculateFrameFocusY(scroll_offset_y_, viewport_height);
      focused_messages = msg_element | ftxui::focusPosition(0, focus_y);
    }
    auto messages = focused_messages | ftxui::frame |
                    ftxui::reflect(visible_box_) | ftxui::flex;

    auto scrollbar = ftxui::emptyElement();
    if (util::ShouldShowScrollbar(content_height_, viewport_height)) {
      int track_height = viewport_height;
      int thumb_size = util::CalculateThumbSize(content_height_,
                                                viewport_height, track_height);
      int thumb_pos = util::CalculateThumbPosition(
          scroll_offset_y_, content_height_, viewport_height, track_height,
          thumb_size);
      ftxui::Elements track_rows;
      for (int i = 0; i < track_height; ++i) {
        if (i >= thumb_pos && i < thumb_pos + thumb_size) {
          track_rows.push_back(ftxui::text("█") |
                               ftxui::color(k_theme.chrome.border));
        } else {
          track_rows.push_back(ftxui::text("│") |
                               ftxui::color(k_theme.chrome.dim_text));
        }
      }
      scrollbar =
          ftxui::vbox(std::move(track_rows)) | ftxui::reflect(scrollbar_box_);
    }

    return ftxui::hbox({
        messages | ftxui::flex,
        scrollbar,
    });
  });

  return ftxui::CatchEvent(content, [this](ftxui::Event event) {
    if (event.is_mouse()) {
      if (captured_mouse_) {
        if (event.mouse().motion == ftxui::Mouse::Released) {
          captured_mouse_ = nullptr;
          scrollbar_dragging_ = false;
          return true;
        }
        int content_height = content_height_;
        int viewport_height = ViewportHeight();
        if (content_height > 0 && viewport_height > 0 &&
            scrollbar_box_.y_max >= scrollbar_box_.y_min) {
          int track_height = viewport_height;
          int thumb_size = util::CalculateThumbSize(
              content_height, viewport_height, track_height);
          int track_usable = track_height - thumb_size;
          if (track_usable > 0) {
            int mouse_y =
                event.mouse().y - scrollbar_box_.y_min - thumb_size / 2;
            float ratio =
                static_cast<float>(mouse_y) / static_cast<float>(track_usable);
            ratio = std::max(0.0F, std::min(1.0F, ratio));
            scroll_offset_y_ = util::CalculateScrollOffsetFromRatio(
                ratio, content_height, viewport_height);
            follow_tail_ = scroll_offset_y_ >= MaxScrollOffset();
          }
        }
        return true;
      }

      if (event.mouse().button == ftxui::Mouse::Left &&
          event.mouse().motion == ftxui::Mouse::Pressed) {
        if (scrollbar_box_.Contain(event.mouse().x, event.mouse().y)) {
          if (event.screen_ != nullptr) {
            captured_mouse_ = event.screen_->CaptureMouse();
          }
          if (captured_mouse_) {
            scrollbar_dragging_ = true;
            int content_height = content_height_;
            int viewport_height = ViewportHeight();
            if (content_height > 0 && viewport_height > 0 &&
                scrollbar_box_.y_max >= scrollbar_box_.y_min) {
              int track_height = viewport_height;
              int thumb_size = util::CalculateThumbSize(
                  content_height, viewport_height, track_height);
              int track_usable = track_height - thumb_size;
              if (track_usable > 0) {
                int mouse_y =
                    event.mouse().y - scrollbar_box_.y_min - thumb_size / 2;
                float ratio = static_cast<float>(mouse_y) /
                              static_cast<float>(track_usable);
                ratio = std::max(0.0F, std::min(1.0F, ratio));
                scroll_offset_y_ = util::CalculateScrollOffsetFromRatio(
                    ratio, content_height, viewport_height);
                follow_tail_ = scroll_offset_y_ >= MaxScrollOffset();
              }
            }
            return true;
          }
        }
      }

      switch (event.mouse().button) {
        case ftxui::Mouse::WheelUp:
          ScrollUp(3);
          return true;
        case ftxui::Mouse::WheelDown:
          ScrollDown(3);
          return true;
        default:
          break;
      }
    }

    if (event == ftxui::Event::PageUp) {
      ScrollUp(PageLines());
      return true;
    }
    if (event == ftxui::Event::PageDown) {
      ScrollDown(PageLines());
      return true;
    }
    if (IsHome(event)) {
      scroll_offset_y_ = 0;
      follow_tail_ = false;
      return true;
    }
    if (IsEnd(event)) {
      scroll_offset_y_ = MaxScrollOffset();
      follow_tail_ = true;
      return true;
    }
    return false;
  });
}

ftxui::Element ChatUI::RenderMessages() const {
  if (session_.Empty() && !is_typing_) {
    auto hint = ftxui::vbox({
        ftxui::text("  // No messages yet") |
            ftxui::color(k_theme.syntax.comment),
        ftxui::text("  // Type something below to start") |
            ftxui::color(k_theme.syntax.comment),
    });
    return ftxui::center(hint | ftxui::borderRounded |
                         ftxui::color(k_theme.chrome.border)) |
           ftxui::flex;
  }

  int current_width = ftxui::Terminal::Size().dimx;
  if (current_width != last_terminal_width_) {
    for (const auto& msg : session_.Messages()) {
      if (msg.sender != Sender::Tool) {
        msg.render_cache.ResetElement();
      }
    }
    last_terminal_width_ = current_width;
  }

  return MessageRenderer::RenderAll(
      session_.Messages(), RenderContext{.terminal_width = current_width,
                                         .thinking_frame = thinking_frame_});
}

void ChatUI::SyncMessageComponents() {
  int current_width = ftxui::Terminal::Size().dimx;
  if (current_width != last_terminal_width_) {
    for (const auto& msg : session_.Messages()) {
      if (msg.sender != Sender::Tool) {
        msg.render_cache.ResetElement();
      }
    }
    last_terminal_width_ = current_width;
  }

  const auto& messages = session_.Messages();
  while (message_components_.size() < messages.size()) {
    const auto index = message_components_.size();
    if (messages[index].sender == Sender::Tool) {
      size_t current_tool_index = 0;
      for (size_t i = 0; i <= index; ++i) {
        if (messages[i].sender == Sender::Tool) {
          current_tool_index = current_tool_index + 1;
        }
      }
      auto content = ftxui::Renderer([this, index] {
        const auto* tool_call = session_.Messages()[index].ToolCall();
        if (tool_call == nullptr) {
          return ftxui::text("Tool call unavailable");
        }
        return tool_call::ToolCallRenderer::Render(
            *tool_call, RenderContext{.terminal_width = last_terminal_width_,
                                      .thinking_frame = thinking_frame_});
      });
      message_components_.push_back(
          Collapsible(messages[index].DisplayLabel(), std::move(content),
                      session_.ToolExpandedState(current_tool_index - 1)));
      continue;
    }

    message_components_.push_back(ftxui::Renderer([this, index] {
      return MessageRenderer::Render(
          session_.Messages()[index],
          RenderContext{.terminal_width = last_terminal_width_,
                        .thinking_frame = thinking_frame_});
    }));
  }
}

int ChatUI::PageLines() const {
  return ViewportHeight();
}

void ChatUI::ScrollUp(int lines) {
  scroll_offset_y_ = std::max(0, scroll_offset_y_ - lines);
  follow_tail_ = false;
}

void ChatUI::ScrollDown(int lines) {
  scroll_offset_y_ = std::min(scroll_offset_y_ + lines, MaxScrollOffset());
  follow_tail_ = scroll_offset_y_ >= MaxScrollOffset();
}

int ChatUI::ViewportHeight() const {
  int visible = visible_box_.y_max - visible_box_.y_min + 1;
  return std::max(1, visible);
}

int ChatUI::MaxScrollOffset() const {
  return util::CalculateMaxScrollOffset(content_height_, ViewportHeight());
}

bool ChatUI::HasActiveAgentMessage() const {
  return std::any_of(session_.Messages().begin(), session_.Messages().end(),
                     [](const Message& message) {
                       return message.sender == Sender::Agent &&
                              message.status == MessageStatus::Active;
                     });
}

void ChatUI::AdvanceThinkingFrame() {
  thinking_frame_ = (thinking_frame_ + 1) % 4;
}

void ChatUI::ClampScrollOffset() {
  scroll_offset_y_ = util::ClampScrollOffset(scroll_offset_y_, content_height_,
                                             ViewportHeight());
  if (scroll_offset_y_ < MaxScrollOffset()) {
    follow_tail_ = false;
  }
}

}  // namespace yac::presentation
