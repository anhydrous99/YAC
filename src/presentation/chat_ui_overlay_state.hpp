#pragma once

#include "command_palette.hpp"
#include "core_types/tool_call_types.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"
#include "ui_status.hpp"

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace yac::presentation {

struct UsageStats {
  int prompt_tokens = 0;
  int completion_tokens = 0;
  int total_tokens = 0;
};

class ChatUiOverlayState {
 public:
  using OnCommandCallback = std::function<void(const std::string&)>;
  using OnToolApprovalCallback = std::function<void(const std::string&, bool)>;

  void SetOnCommand(OnCommandCallback on_command);
  void SetOnToolApproval(OnToolApprovalCallback on_tool_approval);
  void SetCommands(std::vector<Command> commands);
  void SetModelCommands(std::vector<Command> commands);
  void SetProviderModel(std::string provider_id, std::string model);
  void SetLastUsage(UsageStats usage);
  void SetContextWindowTokens(int tokens);
  void SetStartupStatus(StartupStatus status);
  void SetTransientStatus(UiNotice notice);
  void SetQueueDepth(int queue_depth);
  void SetHelpText(std::string help_text);
  void ShowHelp();
  void ShowToolApproval(
      std::string approval_id, std::string tool_name, std::string prompt,
      std::optional<::yac::tool_call::ToolCallBlock> preview = std::nullopt);

  [[nodiscard]] ftxui::Component Wrap(ftxui::Component main_ui);
  [[nodiscard]] bool HandleGlobalEvent(const ftxui::Event& event);
  [[nodiscard]] const std::string& ProviderId() const;
  [[nodiscard]] const std::string& Model() const;
  [[nodiscard]] const std::optional<UsageStats>& LastUsage() const;
  [[nodiscard]] int ContextWindowTokens() const;
  [[nodiscard]] const StartupStatus& Startup() const;
  [[nodiscard]] const std::optional<UiNotice>& TransientStatus() const;
  [[nodiscard]] int QueueDepth() const;

 private:
  void SyncPaletteVisibility();
  void HandleCommandSelection(int index);
  void HandleModelSelection(int index);
  void DispatchToolApproval(bool approved);

  OnCommandCallback on_command_;
  OnToolApprovalCallback on_tool_approval_;
  int palette_level_ = -1;
  bool show_palette_ = false;
  bool show_model_palette_ = false;
  bool show_tool_approval_ = false;
  bool show_help_ = false;
  std::string approval_id_;
  std::string approval_tool_name_;
  std::string approval_prompt_;
  std::optional<::yac::tool_call::ToolCallBlock> approval_preview_;
  std::vector<Command> commands_;
  std::vector<Command> model_commands_;
  std::string provider_id_;
  std::string model_;
  std::optional<UsageStats> last_usage_;
  int context_window_tokens_ = 0;
  StartupStatus startup_;
  std::optional<UiNotice> transient_status_;
  int queue_depth_ = 0;
  std::string help_text_;
};

}  // namespace yac::presentation
