#pragma once

#include "command_palette.hpp"
#include "ftxui/component/component.hpp"
#include "ftxui/component/event.hpp"

#include <functional>
#include <string>
#include <vector>

namespace yac::presentation {

class ChatUiOverlayState {
 public:
  using OnCommandCallback = std::function<void(const std::string&)>;
  using OnToolApprovalCallback = std::function<void(const std::string&, bool)>;

  void SetOnCommand(OnCommandCallback on_command);
  void SetOnToolApproval(OnToolApprovalCallback on_tool_approval);
  void SetCommands(std::vector<Command> commands);
  void SetModelCommands(std::vector<Command> commands);
  void SetProviderModel(std::string provider_id, std::string model);
  void ShowToolApproval(std::string approval_id, std::string tool_name,
                        std::string prompt);

  [[nodiscard]] ftxui::Component Wrap(ftxui::Component main_ui);
  [[nodiscard]] bool HandleGlobalEvent(const ftxui::Event& event);
  [[nodiscard]] const std::string& ProviderId() const;
  [[nodiscard]] const std::string& Model() const;

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
  std::string approval_id_;
  std::string approval_tool_name_;
  std::string approval_prompt_;
  std::vector<Command> commands_;
  std::vector<Command> model_commands_;
  std::string provider_id_;
  std::string model_;
};

}  // namespace yac::presentation
