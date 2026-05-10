#pragma once

#include "command_palette.hpp"
#include "core_types/tool_call_types.hpp"
#include "core_types/typed_ids.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ui_status.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace yac::presentation {

enum class SubPaletteKind { None, Model, Theme };

struct UsageStats {
  int prompt_tokens = 0;
  int completion_tokens = 0;
  int total_tokens = 0;
};

class ChatUiOverlayState {
 public:
  using OnCommandCallback = std::function<void(const std::string&)>;
  using OnToolApprovalCallback =
      std::function<void(const ::yac::ApprovalId&, bool)>;
  using OnAskUserSubmitCallback =
      std::function<void(::yac::ApprovalId approval_id, std::string response)>;
  using OnAskUserCancelCallback =
      std::function<void(::yac::ApprovalId approval_id)>;

  void SetOnCommand(OnCommandCallback on_command);
  void SetOnToolApproval(OnToolApprovalCallback on_tool_approval);
  void SetOnAskUserSubmit(OnAskUserSubmitCallback on_submit);
  void SetOnAskUserCancel(OnAskUserCancelCallback on_cancel);
  void SetCommands(std::vector<Command> commands);
  void SetModelCommands(std::vector<Command> commands);
  void SetThemeCommands(std::vector<Command> commands);
  void SetProviderModel(::yac::ProviderId provider_id, ::yac::ModelId model);
  void SetLastUsage(UsageStats usage);
  void SetContextWindowTokens(int tokens);
  void SetStartupStatus(StartupStatus status);
  void SetQueueDepth(int queue_depth);
  void SetHelpText(std::string help_text);
  void ShowHelp();
  void ShowToolApproval(
      ::yac::ApprovalId approval_id, std::string tool_name, std::string prompt,
      std::optional<::yac::tool_call::ToolCallBlock> preview = std::nullopt);
  void ShowAskUserDialog(::yac::ApprovalId approval_id, std::string question,
                         std::vector<std::string> options);

  [[nodiscard]] ftxui::Component Wrap(ftxui::Component main_ui);
  [[nodiscard]] bool HandleGlobalEvent(const ftxui::Event& event);
  [[nodiscard]] const std::string& ProviderId() const;
  [[nodiscard]] const std::string& Model() const;
  [[nodiscard]] const std::optional<UsageStats>& LastUsage() const;
  [[nodiscard]] int ContextWindowTokens() const;
  [[nodiscard]] const StartupStatus& Startup() const;
  [[nodiscard]] int QueueDepth() const;

 private:
  void SyncPaletteVisibility();
  void HandleCommandSelection(int index);
  void HandleModelSelection(int index);
  void HandleThemeSelection(int index);
  void DispatchToolApproval(bool approved);
  void DispatchAskUserSubmit();
  void DispatchAskUserCancel();

  OnCommandCallback on_command_;
  OnToolApprovalCallback on_tool_approval_;
  OnAskUserSubmitCallback on_ask_user_submit_;
  OnAskUserCancelCallback on_ask_user_cancel_;
  SubPaletteKind sub_palette_kind_ = SubPaletteKind::None;
  int palette_level_ = -1;
  bool show_palette_ = false;
  bool show_model_palette_ = false;
  bool show_theme_palette_ = false;
  bool show_tool_approval_ = false;
  bool show_ask_user_ = false;
  bool show_help_ = false;
  // Tool-approval dialog state. Populated by ShowToolApproval; consumed by
  // DispatchToolApproval. Empty `id` means no approval is currently pending.
  struct ToolApprovalDialog {
    ::yac::ApprovalId id;
    std::string tool_name;
    std::string prompt;
    std::optional<::yac::tool_call::ToolCallBlock> preview;
  };
  ToolApprovalDialog tool_approval_dialog_;

  // Ask-user dialog state. Populated by ShowAskUserDialog; consumed by
  // DispatchAskUserSubmit / DispatchAskUserCancel.
  struct AskUserDialog {
    ::yac::ApprovalId approval_id;
    std::string question;
    std::vector<std::string> options;
    std::string input;
  };
  AskUserDialog ask_user_dialog_;
  std::vector<Command> commands_;
  std::vector<Command> model_commands_;
  std::vector<Command> theme_commands_;
  ::yac::ProviderId provider_id_;
  ::yac::ModelId model_;
  std::optional<UsageStats> last_usage_;
  int context_window_tokens_ = 0;
  StartupStatus startup_;
  int queue_depth_ = 0;
  std::string help_text_;
};

}  // namespace yac::presentation
