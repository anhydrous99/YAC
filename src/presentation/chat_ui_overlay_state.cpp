#include "chat_ui_overlay_state.hpp"

#include "dialog.hpp"
#include "theme.hpp"

namespace yac::presentation {

namespace {

inline const auto& k_theme = theme::Theme::Instance();

}  // namespace

void ChatUiOverlayState::SetOnCommand(OnCommandCallback on_command) {
  on_command_ = std::move(on_command);
}

void ChatUiOverlayState::SetOnToolApproval(
    OnToolApprovalCallback on_tool_approval) {
  on_tool_approval_ = std::move(on_tool_approval);
}

void ChatUiOverlayState::SetCommands(std::vector<Command> commands) {
  commands_ = std::move(commands);
}

void ChatUiOverlayState::SetModelCommands(std::vector<Command> commands) {
  model_commands_ = std::move(commands);
}

void ChatUiOverlayState::SetProviderModel(std::string provider_id,
                                          std::string model) {
  provider_id_ = std::move(provider_id);
  model_ = std::move(model);
}

void ChatUiOverlayState::SetLastUsage(UsageStats usage) {
  last_usage_ = usage;
}

void ChatUiOverlayState::SetContextWindowTokens(int tokens) {
  context_window_tokens_ = tokens;
}

void ChatUiOverlayState::ShowToolApproval(std::string approval_id,
                                          std::string tool_name,
                                          std::string prompt) {
  approval_id_ = std::move(approval_id);
  approval_tool_name_ = std::move(tool_name);
  approval_prompt_ = std::move(prompt);
  show_tool_approval_ = true;
}

ftxui::Component ChatUiOverlayState::Wrap(ftxui::Component main_ui) {
  auto on_select = [this](int index) { HandleCommandSelection(index); };
  auto on_model_select = [this](int index) { HandleModelSelection(index); };

  auto palette = CommandPalette(commands_, on_select, &show_palette_);
  auto dialog = DialogPanel("Command Palette", palette, &show_palette_);
  auto main_component = ftxui::Modal(main_ui, dialog, &show_palette_);

  auto model_palette =
      CommandPalette(model_commands_, on_model_select, &show_model_palette_);
  auto modal_component =
      DialogPanel("Switch Model", model_palette, &show_model_palette_);
  auto main_with_model_picker =
      ftxui::Modal(main_component, modal_component, &show_model_palette_);

  auto approval_content = ftxui::Renderer([this] {
    return ftxui::vbox({
        ftxui::paragraph("Tool: " + approval_tool_name_) |
            ftxui::color(k_theme.dialog.input_fg),
        ftxui::text(""),
        ftxui::paragraph(approval_prompt_) |
            ftxui::color(k_theme.chrome.body_text),
        ftxui::text(""),
        ftxui::text(" Enter/Y=Approve  N/Esc=Reject") |
            ftxui::color(k_theme.dialog.dim_text),
    });
  });
  auto tool_approval_panel =
      DialogPanel("Approve Tool", approval_content, &show_tool_approval_);
  auto approval_modal = ftxui::Modal(main_with_model_picker,
                                     tool_approval_panel, &show_tool_approval_);

  return ftxui::CatchEvent(approval_modal, [this](const ftxui::Event& event) {
    return HandleGlobalEvent(event);
  });
}

bool ChatUiOverlayState::HandleGlobalEvent(const ftxui::Event& event) {
  if (show_tool_approval_) {
    if (event == ftxui::Event::Return ||
        event == ftxui::Event::Character('y') ||
        event == ftxui::Event::Character('Y')) {
      DispatchToolApproval(true);
      return true;
    }
    if (event == ftxui::Event::Escape ||
        event == ftxui::Event::Character('n') ||
        event == ftxui::Event::Character('N')) {
      DispatchToolApproval(false);
      return true;
    }
    return true;
  }

  if (event.input() == "\x10") {
    palette_level_ = 0;
    SyncPaletteVisibility();
    return true;
  }

  return false;
}

const std::string& ChatUiOverlayState::ProviderId() const {
  return provider_id_;
}

const std::string& ChatUiOverlayState::Model() const {
  return model_;
}

const std::optional<UsageStats>& ChatUiOverlayState::LastUsage() const {
  return last_usage_;
}

int ChatUiOverlayState::ContextWindowTokens() const {
  return context_window_tokens_;
}

void ChatUiOverlayState::SyncPaletteVisibility() {
  show_palette_ = palette_level_ >= 0;
  show_model_palette_ = palette_level_ >= 1;
}

void ChatUiOverlayState::HandleCommandSelection(int index) {
  if (index < 0 || static_cast<size_t>(index) >= commands_.size()) {
    return;
  }

  if (commands_[index].id == kSwitchModelCommandId) {
    palette_level_ = 1;
    SyncPaletteVisibility();
    return;
  }

  palette_level_ = -1;
  SyncPaletteVisibility();
  if (on_command_) {
    on_command_(commands_[index].id);
  }
}

void ChatUiOverlayState::HandleModelSelection(int index) {
  if (index >= 0 && static_cast<size_t>(index) < model_commands_.size() &&
      on_command_) {
    on_command_(model_commands_[index].id);
  }

  palette_level_ = -1;
  SyncPaletteVisibility();
}

void ChatUiOverlayState::DispatchToolApproval(bool approved) {
  auto approval_id = approval_id_;
  show_tool_approval_ = false;
  approval_id_.clear();
  approval_tool_name_.clear();
  approval_prompt_.clear();
  if (on_tool_approval_ && !approval_id.empty()) {
    on_tool_approval_(approval_id, approved);
  }
}

}  // namespace yac::presentation
