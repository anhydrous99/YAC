#include "chat_ui_overlay_state.hpp"

#include "dialog.hpp"
#include "theme.hpp"
#include "tool_call/renderer.hpp"
#include "ui_spacing.hpp"

#include <algorithm>
#include <cstddef>
#include <string>
#include <utility>

namespace yac::presentation {

namespace {

ftxui::Element ApprovalToolLabel(const std::string& tool_name) {
  return ftxui::hbox({
      ftxui::text(tool_name) | ftxui::bold |
          ftxui::color(theme::CurrentTheme().semantic.text_strong),
  });
}

ftxui::Element ApprovalArgumentsBlock(const std::string& prompt) {
  return ftxui::vbox({
             ftxui::hbox({ftxui::text(std::string(layout::kCardPadX, ' ')),
                          ftxui::paragraph(prompt) |
                              ftxui::color(theme::CurrentTheme().code.fg) |
                              ftxui::flex,
                          ftxui::text(std::string(layout::kCardPadX, ' '))}),
         }) |
         ftxui::bgcolor(theme::CurrentTheme().code.bg) |
         ftxui::color(theme::CurrentTheme().code.fg);
}

std::string PrettyPrintJson(std::string_view json, size_t max_bytes) {
  std::string out;
  out.reserve(json.size());
  int depth = 0;
  bool in_string = false;
  for (size_t i = 0; i < json.size(); ++i) {
    const char c = json[i];
    if (out.size() >= max_bytes) {
      const auto excess = json.size() - i;
      out += "\n[+" + std::to_string(excess) + " bytes truncated]";
      return out;
    }
    if (in_string) {
      out += c;
      if (c == '\\' && i + 1 < json.size()) {
        out += json[++i];
      } else if (c == '"') {
        in_string = false;
      }
      continue;
    }
    if (c == '"') {
      in_string = true;
      out += c;
      continue;
    }
    switch (c) {
      case '{':
      case '[':
        out += c;
        out += '\n';
        ++depth;
        out.append(static_cast<std::size_t>(depth * 2), ' ');
        break;
      case '}':
      case ']':
        out += '\n';
        --depth;
        out.append(static_cast<std::size_t>(depth * 2), ' ');
        out += c;
        break;
      case ',':
        out += c;
        out += '\n';
        out.append(static_cast<std::size_t>(depth * 2), ' ');
        break;
      case ':':
        out += ": ";
        break;
      case ' ':
      case '\n':
      case '\r':
      case '\t':
        break;
      default:
        out += c;
        break;
    }
  }
  return out;
}

ftxui::Element McpServerBanner(const std::string& server_id) {
  const auto& t = theme::CurrentTheme();
  return ftxui::hbox({
      ftxui::text("MCP: ") | ftxui::bold | ftxui::color(t.role.agent),
      ftxui::text(server_id) | ftxui::bold |
          ftxui::color(t.semantic.text_strong),
  });
}

ftxui::Element McpArgsBlock(const std::string& arguments_json) {
  constexpr size_t kArgsCap = 2048;
  auto formatted = PrettyPrintJson(arguments_json, kArgsCap);
  return ApprovalArgumentsBlock(formatted);
}

ftxui::Element McpTrustPolicyLine(
    bool server_requires_approval,
    const std::vector<std::string>& approval_required_tools) {
  const auto& t = theme::CurrentTheme();
  std::string policy_text;
  if (server_requires_approval) {
    policy_text = "Approval required (per-server)";
  } else if (!approval_required_tools.empty()) {
    policy_text = "Approval required (per-tool:";
    for (const auto& tool : approval_required_tools) {
      policy_text += " " + tool + ",";
    }
    if (!policy_text.empty() && policy_text.back() == ',') {
      policy_text.pop_back();
    }
    policy_text += ")";
  } else {
    policy_text = "Default-allow";
  }
  return ftxui::hbox({
      ftxui::text(policy_text) | ftxui::color(t.semantic.text_muted),
  });
}

ftxui::Element ApprovalActions() {
  return ftxui::hbox({
      ftxui::text(" \xe2\x86\xb5 Enter/Y Approve ") | ftxui::bold |
          ftxui::color(theme::CurrentTheme().role.agent),
      ftxui::text(std::string(layout::kRowGap, ' ')),
      ftxui::text(" N/Esc Reject ") | ftxui::bold |
          ftxui::color(theme::CurrentTheme().role.error),
  });
}

ftxui::Element AskUserInputField(const std::string& input) {
  const auto& t = theme::CurrentTheme();
  auto prompt = ftxui::text("> " + input + "\xe2\x96\x81") |
                ftxui::color(t.dialog.input_fg) |
                ftxui::bgcolor(t.dialog.input_bg);
  return ftxui::hbox({
      ftxui::text(std::string(layout::kCardPadX, ' ')),
      prompt | ftxui::flex,
      ftxui::text(std::string(layout::kCardPadX, ' ')),
  });
}

ftxui::Element AskUserActions() {
  const auto& t = theme::CurrentTheme();
  return ftxui::hbox({
      ftxui::text(" \xe2\x86\xb5 Enter Submit ") | ftxui::bold |
          ftxui::color(t.role.agent),
      ftxui::text(std::string(layout::kRowGap, ' ')),
      ftxui::text(" Esc Cancel ") | ftxui::bold | ftxui::color(t.role.error),
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

void ChatUiOverlayState::SetOnAskUserSubmit(OnAskUserSubmitCallback on_submit) {
  on_ask_user_submit_ = std::move(on_submit);
}

void ChatUiOverlayState::SetOnAskUserCancel(OnAskUserCancelCallback on_cancel) {
  on_ask_user_cancel_ = std::move(on_cancel);
}

void ChatUiOverlayState::SetCommands(std::vector<Command> commands) {
  commands_ = std::move(commands);
}

void ChatUiOverlayState::SetModelCommands(std::vector<Command> commands) {
  model_commands_ = std::move(commands);
}

void ChatUiOverlayState::SetThemeCommands(std::vector<Command> commands) {
  theme_commands_ = std::move(commands);
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
    std::optional<::yac::tool_call::ToolCallBlock> preview) {
  approval_id_ = std::move(approval_id);
  approval_tool_name_ = std::move(tool_name);
  approval_prompt_ = std::move(prompt);
  approval_preview_ = std::move(preview);
  show_tool_approval_ = true;
}

void ChatUiOverlayState::ShowAskUserDialog(std::string approval_id,
                                           std::string question,
                                           std::vector<std::string> options) {
  ask_user_approval_id_ = std::move(approval_id);
  ask_user_question_ = std::move(question);
  ask_user_options_ = std::move(options);
  ask_user_input_.clear();
  show_ask_user_ = true;
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

  auto on_theme_select = [this](int index) { HandleThemeSelection(index); };
  auto theme_palette = CommandPalette([this] { return theme_commands_; },
                                      on_theme_select, &show_theme_palette_);
  auto theme_modal_component =
      DialogPanel("Switch Theme", theme_palette, &show_theme_palette_);
  auto main_with_theme_picker = DialogModal(
      main_with_model_picker, theme_modal_component, &show_theme_palette_);

  auto help_content = ftxui::Renderer([this] {
    return ftxui::paragraph(help_text_.empty()
                                ? std::string{"No help available."}
                                : help_text_) |
           ftxui::color(theme::CurrentTheme().semantic.text_body);
  });
  auto help_panel = DialogPanel("Help", help_content, &show_help_);
  auto main_with_help =
      DialogModal(main_with_theme_picker, help_panel, &show_help_);

  auto approval_content = ftxui::Renderer([this] {
    ftxui::Elements rows{
        ApprovalToolLabel(approval_tool_name_),
        ftxui::text(""),
    };
    const auto* mcp =
        approval_preview_.has_value()
            ? std::get_if<::yac::tool_call::McpToolCall>(&*approval_preview_)
            : nullptr;
    if (mcp != nullptr) {
      rows.push_back(McpServerBanner(mcp->server_id));
      rows.push_back(ftxui::text(""));
      rows.push_back(McpArgsBlock(mcp->arguments_json));
      rows.push_back(ftxui::text(""));
      rows.push_back(McpTrustPolicyLine(mcp->server_requires_approval,
                                        mcp->approval_required_tools));
      rows.push_back(ftxui::text(""));
    } else {
      rows.push_back(ApprovalArgumentsBlock(approval_prompt_));
      rows.push_back(ftxui::text(""));
    }
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

  auto ask_user_content = ftxui::Renderer([this] {
    const auto& t = theme::CurrentTheme();
    ftxui::Elements rows;
    rows.push_back(ftxui::paragraph(ask_user_question_) |
                   ftxui::color(t.semantic.text_strong) | ftxui::bold);
    rows.push_back(ftxui::text(""));
    if (!ask_user_options_.empty()) {
      for (const auto& opt : ask_user_options_) {
        rows.push_back(ftxui::hbox({
            ftxui::text("  \xe2\x80\xa2 ") |
                ftxui::color(t.semantic.text_muted),
            ftxui::paragraph(opt) | ftxui::color(t.semantic.text_body) |
                ftxui::flex,
        }));
      }
      rows.push_back(ftxui::text(""));
    }
    rows.push_back(AskUserInputField(ask_user_input_));
    rows.push_back(ftxui::text(""));
    rows.push_back(AskUserActions());
    return ftxui::vbox(std::move(rows));
  });
  auto ask_user_panel =
      DialogPanel("Ask User", ask_user_content, &show_ask_user_);
  auto ask_user_modal =
      DialogModal(approval_modal, ask_user_panel, &show_ask_user_);

  return ftxui::CatchEvent(ask_user_modal, [this](const ftxui::Event& event) {
    return HandleGlobalEvent(event);
  });
}

bool ChatUiOverlayState::HandleGlobalEvent(const ftxui::Event& event) {
  if (show_ask_user_) {
    if (event == ftxui::Event::Return) {
      DispatchAskUserSubmit();
      return true;
    }
    if (event == ftxui::Event::Escape) {
      DispatchAskUserCancel();
      return true;
    }
    if (event == ftxui::Event::Backspace) {
      if (!ask_user_input_.empty()) {
        ask_user_input_.pop_back();
      }
      return true;
    }
    if (event.is_character()) {
      ask_user_input_ += event.character();
      return true;
    }
    return true;
  }

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
  show_model_palette_ =
      palette_level_ >= 1 && sub_palette_kind_ == SubPaletteKind::Model;
  show_theme_palette_ =
      palette_level_ >= 1 && sub_palette_kind_ == SubPaletteKind::Theme;
}

void ChatUiOverlayState::HandleCommandSelection(int index) {
  if (index < 0 || static_cast<size_t>(index) >= commands_.size()) {
    return;
  }

  if (commands_[index].id == kSwitchModelCommandId) {
    sub_palette_kind_ = SubPaletteKind::Model;
    palette_level_ = 1;
    SyncPaletteVisibility();
    return;
  }

  if (commands_[index].id == kSwitchThemeCommandId) {
    sub_palette_kind_ = SubPaletteKind::Theme;
    palette_level_ = 1;
    SyncPaletteVisibility();
    return;
  }

  sub_palette_kind_ = SubPaletteKind::None;
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

  sub_palette_kind_ = SubPaletteKind::None;
  palette_level_ = -1;
  SyncPaletteVisibility();
}

void ChatUiOverlayState::HandleThemeSelection(int index) {
  if (index >= 0 && static_cast<size_t>(index) < theme_commands_.size() &&
      on_command_) {
    on_command_(theme_commands_[index].id);
  }

  sub_palette_kind_ = SubPaletteKind::None;
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

void ChatUiOverlayState::DispatchAskUserSubmit() {
  auto approval_id = ask_user_approval_id_;
  auto response = ask_user_input_;
  show_ask_user_ = false;
  ask_user_approval_id_.clear();
  ask_user_question_.clear();
  ask_user_options_.clear();
  ask_user_input_.clear();
  if (on_ask_user_submit_ && !approval_id.empty()) {
    on_ask_user_submit_(std::move(approval_id), std::move(response));
  }
}

void ChatUiOverlayState::DispatchAskUserCancel() {
  auto approval_id = ask_user_approval_id_;
  show_ask_user_ = false;
  ask_user_approval_id_.clear();
  ask_user_question_.clear();
  ask_user_options_.clear();
  ask_user_input_.clear();
  if (on_ask_user_cancel_ && !approval_id.empty()) {
    on_ask_user_cancel_(std::move(approval_id));
  }
}

}  // namespace yac::presentation
