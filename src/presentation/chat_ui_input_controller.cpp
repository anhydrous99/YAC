#include "chat_ui_input_controller.hpp"

#include "slash_command_menu.hpp"

namespace yac::presentation {

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

bool IsShiftTab(const ftxui::Event& event) {
  if (event == ftxui::Event::TabReverse) {
    return true;
  }
  if (event.is_mouse() || event.input().empty()) {
    return false;
  }
  const auto& seq = event.input();
  return seq == "\x1b[Z" || seq == "\x1b[9;2u";
}

bool IsSlashArgumentWhitespace(char value) {
  return value == ' ' || value == '\t' || value == '\n' || value == '\r' ||
         value == '\f' || value == '\v';
}

std::string ExtractSlashArguments(const std::string& content) {
  auto args_start = content.find_first_of(" \t\n\r\f\v");
  if (args_start == std::string::npos) {
    return {};
  }
  while (args_start < content.size() &&
         IsSlashArgumentWhitespace(content[args_start])) {
    ++args_start;
  }
  return content.substr(args_start);
}

}  // namespace

ChatUiInputController::ChatUiInputController(
    ComposerState& composer, SlashCommandRegistry& slash_commands)
    : composer_(&composer), slash_commands_(&slash_commands) {}

void ChatUiInputController::SetOnModeToggle(
    std::function<void()> on_mode_toggle) {
  on_mode_toggle_ = std::move(on_mode_toggle);
}

bool ChatUiInputController::HandleEvent(
    const ftxui::Event& event, const std::function<void()>& submit_message,
    const std::function<void()>& insert_newline) {
  if (HandleSlashMenuEvent(event)) {
    return true;
  }

  if (IsShiftTab(event) && !composer_->IsSlashMenuActive()) {
    if (on_mode_toggle_) {
      on_mode_toggle_();
    }
    return true;
  }

  if (event == ftxui::Event::Return) {
    submit_message();
    return true;
  }
  if (IsAltEnter(event) || IsShiftEnter(event) || IsCtrlEnter(event)) {
    insert_newline();
    return true;
  }
  return false;
}

void ChatUiInputController::UpdateSlashMenuState() {
  const auto& content = composer_->Content();
  if (content.empty() || content.front() != '/') {
    composer_->DismissSlashMenu();
    return;
  }
  if (!composer_->IsSlashMenuActive()) {
    composer_->ActivateSlashMenu();
  }
}

ftxui::Element ChatUiInputController::RenderSlashMenu(
    int terminal_width) const {
  if (!composer_->IsSlashMenuActive() || slash_commands_->Commands().empty()) {
    return ftxui::emptyElement();
  }

  auto filtered = composer_->FilteredSlashIndices(slash_commands_->Commands());
  return RenderSlashCommandMenu(slash_commands_->Commands(), filtered,
                                composer_->SlashMenuSelectedIndex(),
                                terminal_width - 4);
}

bool ChatUiInputController::HandleSlashMenuEvent(const ftxui::Event& event) {
  if (!composer_->IsSlashMenuActive()) {
    return false;
  }

  if (event == ftxui::Event::Escape) {
    composer_->DismissSlashMenu();
    return true;
  }

  if (event == ftxui::Event::Return) {
    DispatchSlashMenuSelection();
    return true;
  }

  if (event == ftxui::Event::ArrowUp || event == ftxui::Event::Tab) {
    MoveSlashMenuSelection(-1);
    return true;
  }

  if (event == ftxui::Event::ArrowDown) {
    MoveSlashMenuSelection(1);
    return true;
  }

  return false;
}

void ChatUiInputController::DispatchSlashMenuSelection() {
  auto filtered = composer_->FilteredSlashIndices(slash_commands_->Commands());
  int selected = composer_->SlashMenuSelectedIndex();
  if (selected < 0 || selected >= static_cast<int>(filtered.size())) {
    composer_->DismissSlashMenu();
    return;
  }

  const auto& command = slash_commands_->Commands()[filtered[selected]];
  std::string content = composer_->Submit();
  if (command.arguments_handler.has_value()) {
    auto args = ExtractSlashArguments(content);
    (*command.arguments_handler)(std::move(args));
  } else if (command.handler.has_value()) {
    (*command.handler)();
  }
}

void ChatUiInputController::MoveSlashMenuSelection(int delta) {
  auto filtered = composer_->FilteredSlashIndices(slash_commands_->Commands());
  if (filtered.empty()) {
    return;
  }

  int count = static_cast<int>(filtered.size());
  int current = composer_->SlashMenuSelectedIndex();
  composer_->SetSlashMenuSelectedIndex((current + delta + count) % count);
}

}  // namespace yac::presentation
