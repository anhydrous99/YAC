#include "chat_ui_overlay_state.hpp"

#include "dialog.hpp"
#include "theme.hpp"
#include "tool_call/renderer.hpp"

#include <utility>

namespace yac::presentation {

namespace {

inline const auto& k_theme = theme::Theme::Instance();

ftxui::Element ApprovalToolLabel(const std::string& tool_name) {
  return ftxui::hbox({
      ftxui::text("Tool: ") | ftxui::color(k_theme.dialog.dim_text),
      ftxui::text(tool_name) | ftxui::bold |
          ftxui::color(k_theme.dialog.input_fg),
  });
}

ftxui::Element ApprovalActions() {
  return ftxui::hbox({
      ftxui::text(" Enter/Y Approve ") | ftxui::bold |
          ftxui::color(k_theme.role.agent),
      ftxui::text("  "),
      ftxui::text(" N/Esc Reject ") | ftxui::bold |
          ftxui::color(k_theme.role.error),
  });
}

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
  startup_.provider_id = provider_id_;
  startup_.model = model_;
}

void ChatUiOverlayState::SetLastUsage(UsageStats usage) {
  last_usage_ = usage;
}

void ChatUiOverlayState::SetContextWindowTokens(int tokens) {
  context_window_tokens_ = tokens;
}

void ChatUiOverlayState::SetStartupStatus(StartupStatus status) {
  startup_ = std::move(status);
}

void ChatUiOverlayState::SetTransientStatus(UiNotice notice) {
  transient_status_ = std::move(notice);
}

void ChatUiOverlayState::SetQueueDepth(int queue_depth) {
  queue_depth_ = queue_depth;
}

void ChatUiOverlayState::SetHelpText(std::string help_text) {
  help_text_ = std::move(help_text);
}

void ChatUiOverlayState::ShowHelp() {
  show_help_ = true;
}

void ChatUiOverlayState::ShowToolApproval(
    std::string approval_id, std::string tool_name, std::string prompt,
    std::optional< ::yac::tool_call::ToolCallBlock> preview) {
  approval_id_ = std::move(approval_id);
  approval_tool_name_ = std::move(tool_name);
  approval_prompt_ = std::move(prompt);
  approval_preview_ = std::move(preview);
  show_tool_approval_ = true;
}

ftxui::Component ChatUiOverlayState::Wrap(ftxui::Component main_ui) {
  auto on_select = [this](int index) { HandleCommandSelection(index); };
  auto on_model_select = [this](int index) { HandleModelSelection(index); };

  auto palette =
      CommandPalette([this] { return commands_; }, on_select, &show_palette_);
  auto dialog = DialogPanel("Command Palette", palette, &show_palette_);
  auto main_component = DialogModal(main_ui, dialog, &show_palette_);

  auto model_palette = CommandPalette([this] { return model_commands_; },
                                      on_model_select, &show_model_palette_);
  auto modal_component =
      DialogPanel("Switch Model", model_palette, &show_model_palette_);
  auto main_with_model_picker =
      DialogModal(main_component, modal_component, &show_model_palette_);

  auto help_content = ftxui::Renderer([this] {
    return ftxui::paragraph(help_text_.empty()
                                ? std::string{"No help available."}
                                : help_text_) |
           ftxui::color(k_theme.chrome.body_text);
  });
  auto help_panel = DialogPanel("Help", help_content, &show_help_);
  auto main_with_help =
      DialogModal(main_with_model_picker, help_panel, &show_help_);

  auto approval_content = ftxui::Renderer([this] {
    ftxui::Elements rows{
        ApprovalToolLabel(approval_tool_name_),
        ftxui::text(""),
        ftxui::paragraph(approval_prompt_) |
            ftxui::color(k_theme.chrome.body_text),
        ftxui::text(""),
    };
    if (approval_preview_.has_value()) {
      rows.push_back(tool_call::ToolCallRenderer::Render(
          *approval_preview_, RenderContext{.terminal_width = 72}));
      rows.push_back(ftxui::text(""));
    }
    rows.push_back(ApprovalActions());
    return ftxui::vbox(std::move(rows));
  });
  auto tool_approval_panel = DialogPanel(
      "Permission Required", approval_content, &show_tool_approval_);
  auto approval_modal =
      DialogModal(main_with_help, tool_approval_panel, &show_tool_approval_);

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

const StartupStatus& ChatUiOverlayState::Startup() const {
  return startup_;
}

const std::optional<UiNotice>& ChatUiOverlayState::TransientStatus() const {
  return transient_status_;
}

int ChatUiOverlayState::QueueDepth() const {
  return queue_depth_;
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
  approval_preview_.reset();
  if (on_tool_approval_ && !approval_id.empty()) {
    on_tool_approval_(approval_id, approved);
  }
}

}  // namespace yac::presentation
