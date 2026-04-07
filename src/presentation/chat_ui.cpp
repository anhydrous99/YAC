#include "chat_ui.hpp"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/mouse.hpp"
#include "ftxui/dom/elements.hpp"
#include "markdown/parser.hpp"
#include "theme.hpp"

#include <algorithm>
#include <numeric>
#include <utility>

namespace yac::presentation {

inline const auto& k_theme = theme::Theme::Instance();

namespace {

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

int CountNewlines(const std::string& text) {
  return static_cast<int>(
      std::accumulate(text.begin(), text.end(), 0,
                      [](int count, char ch) { return count + (ch == '\n'); }));
}

}  // namespace

ChatUI::ChatUI() : on_send_([](const std::string&) {}) {}

ChatUI::ChatUI(OnSendCallback on_send) : on_send_(std::move(on_send)) {}

ftxui::Component ChatUI::Build() {
  auto message_list = BuildMessageList();
  auto input = BuildInput();

  auto container = ftxui::Container::Stacked({message_list, input});

  return ftxui::Renderer(container, [this, message_list, input] {
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

    auto input_height = CalculateInputHeight();

    return ftxui::vbox({
               message_list->Render() | ftxui::flex,
               ftxui::separator() | ftxui::color(k_theme.markdown.separator),
               ftxui::hbox(footer_elements),
               ftxui::separator() | ftxui::color(k_theme.markdown.separator),
               input_area | ftxui::bgcolor(k_theme.cards.user_bg) |
                   ftxui::size(ftxui::HEIGHT, ftxui::GREATER_THAN,
                               input_height),
           }) |
           ftxui::borderRounded | ftxui::color(k_theme.chrome.border);
  });
}

void ChatUI::AddMessage(Sender sender, std::string content) {
  Message msg{sender, std::move(content)};
  if (sender == Sender::Agent) {
    msg.cached_blocks = markdown::MarkdownParser::Parse(msg.content);
  }
  messages_.push_back(std::move(msg));
  scroll_focus_y_ = 10000;
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
  input_cursor_ = 0;
  on_send_(sent);
}

ftxui::Component ChatUI::BuildInput() {
  ftxui::InputOption option;
  option.multiline = true;
  option.placeholder = "Type a message...";
  option.cursor_position = &input_cursor_;
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

  auto input = ftxui::Input(&input_content_, option);

  return ftxui::CatchEvent(input, [this](const ftxui::Event& event) {
    return HandleInputEvent(event);
  });
}

int ChatUI::CalculateInputHeight() const {
  if (input_content_.empty()) {
    return 1;
  }
  int lines = CountNewlines(input_content_) + 1;
  return std::min(lines, kMaxInputLines);
}

void ChatUI::InsertNewline() {
  input_content_.insert(static_cast<size_t>(input_cursor_), "\n");
  ++input_cursor_;
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
  auto content = ftxui::Renderer([this] {
    return RenderMessages() | ftxui::focusPosition(0, scroll_focus_y_) |
           ftxui::frame | ftxui::reflect(visible_box_) | ftxui::flex |
           ftxui::vscroll_indicator | ftxui::color(k_theme.chrome.dim_text);
  });

  return ftxui::CatchEvent(content, [this](ftxui::Event event) {
    if (event.is_mouse()) {
      switch (event.mouse().button) {
        case ftxui::Mouse::WheelUp:
          ScrollUp(3);
          return true;
        case ftxui::Mouse::WheelDown:
          ScrollDown(3);
          return true;
        default:
          return false;
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
    return false;
  });
}

ftxui::Element ChatUI::RenderMessages() const {
  if (messages_.empty() && !is_typing_) {
    return ftxui::vbox({ftxui::text("No messages yet.") | ftxui::dim |
                        ftxui::color(k_theme.chrome.dim_text)}) |
           ftxui::flex;
  }

  return MessageRenderer::RenderAll(messages_);
}

int ChatUI::PageLines() const {
  int visible = visible_box_.y_max - visible_box_.y_min + 1;
  return std::max(1, visible);
}

void ChatUI::ScrollUp(int lines) {
  scroll_focus_y_ = std::max(0, scroll_focus_y_ - lines);
}

void ChatUI::ScrollDown(int lines) {
  scroll_focus_y_ += lines;
}

}  // namespace yac::presentation
