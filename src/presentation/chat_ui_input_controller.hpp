#pragma once

#include "composer_state.hpp"
#include "core_types/file_mention.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/dom/elements.hpp"
#include "slash_command_registry.hpp"

#include <functional>
#include <string_view>
#include <vector>

namespace yac::presentation {

// Provider-returned bundle for the @file menu. `is_indexing` lets the menu
// distinguish "no matches found" from "we haven't built the index yet" — see
// FileIndex::GetState().
struct FileMentionResult {
  std::vector<tool_call::FileMentionRow> rows;
  bool is_indexing = false;
};

class ChatUiInputController {
 public:
  using FileMentionProvider =
      std::function<FileMentionResult(std::string_view prefix)>;

  ChatUiInputController(ComposerState& composer,
                        SlashCommandRegistry& slash_commands);

  [[nodiscard]] bool HandleEvent(const ftxui::Event& event,
                                 const std::function<void()>& submit_message,
                                 const std::function<void()>& insert_newline);
  void SetOnModeToggle(std::function<void()> on_mode_toggle);
  void SetFileMentionProvider(FileMentionProvider provider);

  void UpdateSlashMenuState();
  void UpdateAtMenuState();

  [[nodiscard]] ftxui::Element RenderSlashMenu(int terminal_width) const;
  [[nodiscard]] ftxui::Element RenderAtMenu(int terminal_width) const;

 private:
  [[nodiscard]] bool HandleSlashMenuEvent(const ftxui::Event& event);
  void DispatchSlashMenuSelection();
  void MoveSlashMenuSelection(int delta);

  [[nodiscard]] bool HandleAtMenuEvent(const ftxui::Event& event);
  void DispatchAtMenuSelection();
  void MoveAtMenuSelection(int delta);

  ComposerState* composer_;
  SlashCommandRegistry* slash_commands_;
  FileMentionProvider file_mention_provider_;
  std::vector<tool_call::FileMentionRow> last_at_rows_;
  bool last_at_indexing_ = false;
  std::function<void()> on_mode_toggle_;
};

}  // namespace yac::presentation
