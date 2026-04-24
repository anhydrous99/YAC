#pragma once

#include "composer_state.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "slash_command_registry.hpp"

#include <functional>

namespace yac::presentation {

class ChatUiInputController {
 public:
  ChatUiInputController(ComposerState& composer,
                        SlashCommandRegistry& slash_commands);

  [[nodiscard]] bool HandleEvent(const ftxui::Event& event,
                                 const std::function<void()>& submit_message,
                                 const std::function<void()>& insert_newline);
  void SetOnModeToggle(std::function<void()> on_mode_toggle);
  void UpdateSlashMenuState();
  [[nodiscard]] ftxui::Element RenderSlashMenu(int terminal_width) const;

 private:
  [[nodiscard]] bool HandleSlashMenuEvent(const ftxui::Event& event);
  void DispatchSlashMenuSelection();
  void MoveSlashMenuSelection(int delta);

  ComposerState* composer_;
  SlashCommandRegistry* slash_commands_;
  std::function<void()> on_mode_toggle_;
};

}  // namespace yac::presentation
