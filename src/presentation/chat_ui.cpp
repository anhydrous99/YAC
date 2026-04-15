#include "chat_ui.hpp"

#include "collapsible.hpp"
#include "command_palette.hpp"
#include "dialog.hpp"
#include "ftxui/component/app.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/screen/terminal.hpp"
#include "slash_command_menu.hpp"
#include "theme.hpp"
#include "util/scroll_math.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <string_view>
#include <utility>

namespace yac::presentation {

inline const auto& k_theme = theme::Theme::Instance();

namespace {

constexpr auto kThinkingFrameDuration = std::chrono::milliseconds(260);

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
    const auto child_count = ChildCount();
    bool rebuild = children.size() < child_count;
    if (!rebuild) {
      for (size_t i = 0; i < child_count; ++i) {
        if (children[i] != children_[i]) {
          rebuild = true;
          break;
        }
      }
    }

    if (rebuild) {
      DetachAllChildren();
    }
    const auto start = rebuild ? 0 : child_count;
    for (size_t i = start; i < children.size(); ++i) {
      Add(children[i]);
    }
  }

  std::function<ftxui::Components()> get_children_;
};

class SlashMenuInputWrapper : public ftxui::ComponentBase {
 public:
  SlashMenuInputWrapper(ftxui::Component input,
                        std::function<bool(const ftxui::Event&)> pre_handler,
                        std::function<void()> post_handler)
      : pre_handler_(std::move(pre_handler)),
        post_handler_(std::move(post_handler)) {
    Add(std::move(input));
  }

  bool OnEvent(ftxui::Event event) override {
    if (pre_handler_(event)) {
      return true;
    }
    bool handled = children_.front()->OnEvent(event);
    post_handler_();
    return handled;
  }

 private:
  std::function<bool(const ftxui::Event&)> pre_handler_;
  std::function<void()> post_handler_;
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

struct ChatUI::AnimationState {
  std::atomic_bool alive = true;
};

ChatUI::ChatUI()
    : on_send_([](const std::string&) {}),
      animation_state_(std::make_shared<AnimationState>()) {}

ChatUI::ChatUI(OnSendCallback on_send)
    : on_send_(std::move(on_send)),
      animation_state_(std::make_shared<AnimationState>()) {}

ChatUI::~ChatUI() {
  if (animation_state_) {
    animation_state_->alive.store(false, std::memory_order_release);
  }
  StopThinkingAnimation();
}

void ChatUI::SetOnSend(OnSendCallback on_send) {
  on_send_ = std::move(on_send);
}

void ChatUI::SetOnCommand(OnCommandCallback on_command) {
  on_command_ = std::move(on_command);
}

void ChatUI::SetUiTaskRunner(UiTaskRunner ui_task_runner) {
  ui_task_runner_ = std::move(ui_task_runner);
  SyncThinkingAnimation();
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
    footer_elements.push_back(ftxui::filler());
    if (!provider_id_.empty() || !model_.empty()) {
      auto provider_model = provider_id_;
      if (!provider_model.empty() && !model_.empty()) {
        provider_model += " / ";
      }
      provider_model += model_;
      footer_elements.push_back(ftxui::text("  " + provider_model) |
                                ftxui::color(k_theme.chrome.dim_text) |
                                ftxui::dim);
    }
    if (!session_.Empty()) {
      auto count_label = "  [" + std::to_string(session_.MessageCount()) +
                         " message" + (session_.MessageCount() > 1 ? "s" : "") +
                         "]";
      footer_elements.push_back(ftxui::text(count_label) |
                                ftxui::color(k_theme.chrome.dim_text) |
                                ftxui::dim);
    }

    auto input_area = ftxui::hbox({
        ftxui::text(" > ") | ftxui::color(k_theme.chrome.prompt) |
            ftxui::bold,
        input->Render() | ftxui::flex,
        ftxui::text(" " + std::to_string(composer_.CalculateHeight(kMaxInputLines)) +
                  "/" + std::to_string(kMaxInputLines) + " ") |
            ftxui::color(k_theme.chrome.dim_text) | ftxui::dim,
    });

    auto input_height = CalculateInputHeight();

    ftxui::Element slash_menu;
    if (composer_.IsSlashMenuActive() && !slash_commands_.Commands().empty()) {
      auto filtered =
          composer_.FilteredSlashIndices(slash_commands_.Commands());
      int term_width = ftxui::Terminal::Size().dimx;
      slash_menu = RenderSlashCommandMenu(slash_commands_.Commands(), filtered,
                                          composer_.SlashMenuSelectedIndex(),
                                          term_width - 4);
    }

    ftxui::Elements footer_rows;
    footer_rows.push_back(
        ftxui::text("") |
        ftxui::bgcolor(k_theme.chrome.dim_text));
    footer_rows.push_back(ftxui::hbox(footer_elements));

    footer_rows.push_back(
        ftxui::text(" Enter=Send \xe2\x94\x82 Ctrl+P=Commands \xe2\x94\x82 "
                    "\xe2\x87\xa7+Enter=Newline \xe2\x94\x82 "
                    "PgUp/PgDn \xe2\x94\x82 Home/End") |
        ftxui::color(k_theme.chrome.dim_text) | ftxui::dim);

    auto footer_with_hints = ftxui::vbox(footer_rows);

    ftxui::Elements main_parts;
    main_parts.push_back(message_list->Render() | ftxui::flex);
    main_parts.push_back(footer_with_hints |
                         ftxui::bgcolor(k_theme.cards.agent_bg));
    if (composer_.IsSlashMenuActive() && !slash_commands_.Commands().empty()) {
      main_parts.push_back(slash_menu);
    }
    main_parts.push_back(
        input_area | ftxui::bgcolor(k_theme.cards.user_bg) |
        ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN, input_height));

    return ftxui::vbox(std::move(main_parts));
  });

  auto sync_visibility = [this] {
    show_palette_ = palette_level_ >= 0;
    show_model_palette_ = palette_level_ >= 1;
  };

  auto on_select = [this, sync_visibility](int index) {
    if (index < 0 || static_cast<size_t>(index) >= commands_.size()) {
      return;
    }
    if (commands_[index].id == kSwitchModelCommandId) {
      palette_level_ = 1;
      sync_visibility();
      return;
    }
    palette_level_ = -1;
    sync_visibility();
    if (on_command_) {
      on_command_(commands_[index].id);
    }
  };
  auto on_model_select = [this, sync_visibility](int index) {
    if (index >= 0 && static_cast<size_t>(index) < model_commands_.size() &&
        on_command_) {
      on_command_(model_commands_[index].id);
    }
    palette_level_ = -1;
    sync_visibility();
  };

  auto palette = CommandPalette(commands_, on_select, &show_palette_);
  auto dialog = DialogPanel("Command Palette", palette, &show_palette_);
  auto main_component = ftxui::Modal(main_ui, dialog, &show_palette_);
  auto model_palette =
      CommandPalette(model_commands_, on_model_select, &show_model_palette_);
  auto modal_component =
      DialogPanel("Switch Model", model_palette, &show_model_palette_);
  auto modal =
      ftxui::Modal(main_component, modal_component, &show_model_palette_);

  return ftxui::CatchEvent(modal,
                           [this, sync_visibility](const ftxui::Event& event) {
                             if (event.input() == "\x10") {
                               palette_level_ = 0;
                               sync_visibility();
                               return true;
                             }
                             return false;
                           });
}

MessageId ChatUI::AddMessage(Sender sender, std::string content,
                             MessageStatus status) {
  auto id = session_.AddMessage(sender, std::move(content), status);
  render_cache_.ResetContent(id);
  if (sender == Sender::Agent && status == MessageStatus::Active) {
    thinking_frame_ = 0;
  }
  if (!scrollbar_dragging_) {
    follow_tail_ = true;
  }
  SyncThinkingAnimation();
  return id;
}

MessageId ChatUI::AddMessageWithId(MessageId id, Sender sender,
                                   std::string content, MessageStatus status) {
  auto added_id =
      session_.AddMessageWithId(id, sender, std::move(content), status);
  render_cache_.ResetContent(added_id);
  if (sender == Sender::Agent && status == MessageStatus::Active) {
    thinking_frame_ = 0;
  }
  if (!scrollbar_dragging_) {
    follow_tail_ = true;
  }
  SyncMessageComponents();
  SyncThinkingAnimation();
  return added_id;
}

MessageId ChatUI::StartAgentMessage() {
  auto id = session_.AddMessage(Sender::Agent, "", MessageStatus::Active);
  thinking_frame_ = 0;
  render_cache_.ResetContent(id);
  if (!scrollbar_dragging_) {
    follow_tail_ = true;
  }
  SyncMessageComponents();
  SyncThinkingAnimation();
  return id;
}

MessageId ChatUI::StartAgentMessage(MessageId id) {
  return AddMessageWithId(id, Sender::Agent, "", MessageStatus::Active);
}

void ChatUI::AppendToAgentMessage(MessageId id, std::string delta) {
  if (delta.empty()) {
    return;
  }
  session_.AppendToAgentMessage(id, std::move(delta));
  render_cache_.ResetContent(id);
  if (!scrollbar_dragging_) {
    follow_tail_ = true;
  }
  SyncMessageComponents();
  SyncThinkingAnimation();
}

void ChatUI::SetMessageStatus(MessageId id, MessageStatus status) {
  if (status == MessageStatus::Active) {
    thinking_frame_ = 0;
  }
  session_.SetMessageStatus(id, status);
  render_cache_.ResetElement(id);
  SyncThinkingAnimation();
}

void ChatUI::AddToolCallMessage(::yac::tool_call::ToolCallBlock block) {
  auto id = session_.AddToolCallMessage(std::move(block));
  render_cache_.ResetContent(id);
  if (!scrollbar_dragging_) {
    follow_tail_ = true;
  }
  SyncMessageComponents();
}

void ChatUI::SetCommands(std::vector<Command> commands) {
  commands_ = std::move(commands);
}

void ChatUI::SetModelCommands(std::vector<Command> commands) {
  model_commands_ = std::move(commands);
}

void ChatUI::SetSlashCommands(SlashCommandRegistry registry) {
  slash_commands_ = std::move(registry);
}

void ChatUI::SetProviderModel(std::string provider_id, std::string model) {
  provider_id_ = std::move(provider_id);
  model_ = std::move(model);
}

void ChatUI::SetTyping(bool typing) {
  is_typing_ = typing;
}

void ChatUI::SetToolExpanded(size_t index, bool expanded) {
  session_.SetToolExpanded(index, expanded);
}

void ChatUI::ClearMessages() {
  session_.ClearMessages();
  render_cache_.Clear();
  message_components_.clear();
  messages_seen_count_ = 0;
  SyncThinkingAnimation();
}

const std::vector<Message>& ChatUI::GetMessages() const {
  return session_.Messages();
}

bool ChatUI::HasMessage(MessageId id) const {
  return session_.HasMessage(id);
}

bool ChatUI::IsTyping() const {
  return is_typing_;
}

std::string ChatUI::ProviderId() const {
  return provider_id_;
}

std::string ChatUI::Model() const {
  return model_;
}

void ChatUI::SubmitMessage() {
  if (composer_.Empty()) {
    return;
  }
  std::string sent = composer_.Submit();
  if (slash_commands_.TryDispatch(sent)) {
    return;
  }
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

  return ftxui::Make<SlashMenuInputWrapper>(
      input,
      [this](const ftxui::Event& event) { return HandleInputEvent(event); },
      [this] { UpdateSlashMenuState(); });
}

int ChatUI::CalculateInputHeight() const {
  return composer_.CalculateHeight(kMaxInputLines);
}

void ChatUI::InsertNewline() {
  composer_.InsertNewline();
}

bool ChatUI::HandleInputEvent(const ftxui::Event& event) {
  if (composer_.IsSlashMenuActive()) {
    if (event == ftxui::Event::Escape) {
      composer_.DismissSlashMenu();
      return true;
    }
    if (event == ftxui::Event::Return) {
      DispatchSlashMenuSelection();
      return true;
    }
    if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Tab) {
      auto filtered =
          composer_.FilteredSlashIndices(slash_commands_.Commands());
      if (!filtered.empty()) {
        int current = composer_.SlashMenuSelectedIndex();
        int next = (current - 1 + static_cast<int>(filtered.size())) %
                   static_cast<int>(filtered.size());
        composer_.SetSlashMenuSelectedIndex(next);
      }
      return true;
    }
    if (event == ftxui::Event::ArrowDown) {
      auto filtered =
          composer_.FilteredSlashIndices(slash_commands_.Commands());
      if (!filtered.empty()) {
        int current = composer_.SlashMenuSelectedIndex();
        int next = (current + 1) % static_cast<int>(filtered.size());
        composer_.SetSlashMenuSelectedIndex(next);
      }
      return true;
    }
  }

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
          track_rows.push_back(ftxui::text(" ") |
                               ftxui::bgcolor(k_theme.chrome.dim_text));
        } else {
          track_rows.push_back(ftxui::text(" ") |
                               ftxui::bgcolor(k_theme.cards.agent_bg));
        }
      }
      scrollbar =
          ftxui::vbox(std::move(track_rows)) | ftxui::reflect(scrollbar_box_);
    }

    auto message_area = ftxui::hbox({
        messages | ftxui::flex,
        scrollbar,
    });

    if (!follow_tail_ && session_.MessageCount() > 0) {
      auto new_count =
          session_.MessageCount() - messages_seen_count_;
      std::string label = new_count > 0
                              ? " \xe2\x86\x93 " + std::to_string(new_count) +
                                    " new "
                              : " \xe2\x86\x93 ";
      auto fab = ftxui::text(label) |
                 ftxui::color(k_theme.chrome.dim_text) |
                 ftxui::bgcolor(k_theme.dialog.input_bg) |
                 ftxui::size(ftxui::WIDTH, ftxui::LESS_THAN, 12);
      auto fab_spacer = ftxui::vbox({
          ftxui::filler(),
          fab | ftxui::align_right,
      });
      message_area = ftxui::dbox({message_area, fab_spacer | ftxui::flex});
    }

    return message_area;
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
        ftxui::text(""),
        ftxui::text("  // No messages yet") |
            ftxui::color(k_theme.syntax.comment),
        ftxui::text("  // Type something below to start") |
            ftxui::color(k_theme.syntax.comment),
        ftxui::text(""),
    });
    return ftxui::center(hint | ftxui::bgcolor(k_theme.cards.agent_bg)) |
           ftxui::flex;
  }

  int current_width = ftxui::Terminal::Size().dimx;
  if (current_width != last_terminal_width_) {
    for (const auto& msg : session_.Messages()) {
      if (msg.sender != Sender::Tool) {
        render_cache_.ResetElement(msg.id);
      }
    }
    last_terminal_width_ = current_width;
  }

  return MessageRenderer::RenderAll(
      session_.Messages(), render_cache_,
      RenderContext{.terminal_width = current_width,
                    .thinking_frame = thinking_frame_});
}

void ChatUI::SyncMessageComponents() {
  int current_width = ftxui::Terminal::Size().dimx;
  if (current_width != last_terminal_width_) {
    for (const auto& msg : session_.Messages()) {
      if (msg.sender != Sender::Tool) {
        render_cache_.ResetElement(msg.id);
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
      const auto* block = messages[index].ToolCall();
      auto summary = block != nullptr
                          ? ::yac::presentation::tool_call::ToolCallRenderer::
                                BuildSummary(*block)
                          : "";
      message_components_.push_back(
          Collapsible(messages[index].DisplayLabel(), std::move(content),
                      session_.ToolExpandedState(current_tool_index - 1),
                      std::move(summary)));
      continue;
    }

    message_components_.push_back(ftxui::Renderer([this, index] {
      const auto& message = session_.Messages()[index];
      return MessageRenderer::Render(
          message, render_cache_.For(message.id),
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
  messages_seen_count_ = session_.MessageCount();
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

bool ChatUI::HasPendingAgentMessage() const {
  return std::any_of(session_.Messages().begin(), session_.Messages().end(),
                     [](const Message& message) {
                       return message.sender == Sender::Agent &&
                              message.status == MessageStatus::Active &&
                              message.Text().empty();
                     });
}

void ChatUI::AdvanceThinkingFrame() {
  thinking_frame_ = (thinking_frame_ + 1) % 10;
}

void ChatUI::SyncThinkingAnimation() {
  if (HasPendingAgentMessage()) {
    StartThinkingAnimation();
    return;
  }
  StopThinkingAnimation();
}

void ChatUI::StartThinkingAnimation() {
  if (thinking_animation_worker_.joinable() || !ui_task_runner_ ||
      !animation_state_) {
    return;
  }

  auto ui_task_runner = ui_task_runner_;
  auto animation_state = animation_state_;
  thinking_animation_worker_ =
      std::jthread([this, ui_task_runner = std::move(ui_task_runner),
                    animation_state = std::move(animation_state)](
                       std::stop_token stop_token) mutable {
        while (!stop_token.stop_requested()) {
          std::unique_lock lock(thinking_animation_mutex_);
          thinking_animation_wake_.wait_for(
              lock, stop_token, kThinkingFrameDuration, [] { return false; });
          if (stop_token.stop_requested()) {
            return;
          }
          lock.unlock();

          ui_task_runner([this, animation_state] {
            if (!animation_state->alive.load(std::memory_order_acquire)) {
              return;
            }
            if (!HasPendingAgentMessage()) {
              SyncThinkingAnimation();
              return;
            }
            AdvanceThinkingFrame();
          });
        }
      });
}

void ChatUI::StopThinkingAnimation() {
  if (!thinking_animation_worker_.joinable()) {
    return;
  }

  thinking_animation_worker_.request_stop();
  thinking_animation_wake_.notify_all();
  thinking_animation_worker_ = std::jthread{};
}

void ChatUI::ClampScrollOffset() {
  scroll_offset_y_ = util::ClampScrollOffset(scroll_offset_y_, content_height_,
                                             ViewportHeight());
  if (scroll_offset_y_ < MaxScrollOffset()) {
    follow_tail_ = false;
  }
}

void ChatUI::UpdateSlashMenuState() {
  const auto& content = composer_.Content();
  if (content.empty() || content.front() != '/') {
    composer_.DismissSlashMenu();
    return;
  }
  if (!composer_.IsSlashMenuActive()) {
    composer_.ActivateSlashMenu();
  }
}

bool ChatUI::HandleSlashMenuEvent(const ftxui::Event& event) {
  if (!composer_.IsSlashMenuActive()) {
    return false;
  }

  if (event == ftxui::Event::Escape) {
    composer_.DismissSlashMenu();
    return true;
  }

  if (event == ftxui::Event::Return) {
    DispatchSlashMenuSelection();
    return true;
  }

  auto filtered = composer_.FilteredSlashIndices(slash_commands_.Commands());
  if (filtered.empty()) {
    return false;
  }
  int count = static_cast<int>(filtered.size());

  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Tab) {
    int current = composer_.SlashMenuSelectedIndex();
    composer_.SetSlashMenuSelectedIndex((current - 1 + count) % count);
    return true;
  }
  if (event == ftxui::Event::ArrowDown) {
    int current = composer_.SlashMenuSelectedIndex();
    composer_.SetSlashMenuSelectedIndex((current + 1) % count);
    return true;
  }

  return false;
}

void ChatUI::DispatchSlashMenuSelection() {
  auto filtered = composer_.FilteredSlashIndices(slash_commands_.Commands());
  int selected = composer_.SlashMenuSelectedIndex();
  if (selected < 0 || selected >= static_cast<int>(filtered.size())) {
    composer_.DismissSlashMenu();
    return;
  }
  const auto& command = slash_commands_.Commands()[filtered[selected]];
  (void)composer_.Submit();
  if (command.handler.has_value()) {
    (*command.handler)();
  }
}

}  // namespace yac::presentation
