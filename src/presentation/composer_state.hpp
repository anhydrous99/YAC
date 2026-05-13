#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yac::presentation {

struct SlashCommand;

struct ComposerVisualLine {
  std::string text;
  size_t start = 0;
  size_t end = 0;
};

class ComposerState {
 public:
  [[nodiscard]] bool Empty() const;
  [[nodiscard]] int CalculateHeight(int max_lines) const;
  [[nodiscard]] int CalculateHeight(int max_lines, int wrap_width) const;
  [[nodiscard]] std::vector<ComposerVisualLine> VisualLines(
      int wrap_width) const;

  [[nodiscard]] std::string& Content();
  [[nodiscard]] const std::string& Content() const;
  [[nodiscard]] int* CursorPosition();

  void InsertNewline();
  [[nodiscard]] std::string Submit();

  [[nodiscard]] bool IsSlashMenuActive() const;
  void ActivateSlashMenu();
  void DismissSlashMenu();
  [[nodiscard]] int SlashMenuSelectedIndex() const;
  void SetSlashMenuSelectedIndex(int index);
  [[nodiscard]] std::string SlashMenuFilter() const;
  [[nodiscard]] std::vector<int> FilteredSlashIndices(
      const std::vector<SlashCommand>& commands) const;

  // @file mention menu state.
  [[nodiscard]] bool IsAtMenuActive() const;
  void ActivateAtMenu(size_t at_token_start);
  void DismissAtMenu();
  [[nodiscard]] int AtMenuSelectedIndex() const;
  void SetAtMenuSelectedIndex(int index);
  [[nodiscard]] size_t AtTokenStart() const;
  [[nodiscard]] std::string AtMenuFilter() const;
  void InsertMention(std::string_view relative_path);

  // Returns the offset of an '@' immediately before the cursor when the run
  // [@..cursor) is whitespace-free AND the char before '@' is whitespace or
  // the '@' sits at offset 0. Used to gate the at-menu trigger so that
  // strings like `me@host.com` do not open the menu.
  [[nodiscard]] std::optional<size_t> FindAtTokenAtCursor() const;

 private:
  std::string content_;
  int cursor_ = 0;
  bool slash_menu_active_ = false;
  int slash_menu_selected_ = 0;
  bool at_menu_active_ = false;
  int at_menu_selected_ = 0;
  size_t at_token_start_ = 0;
};

}  // namespace yac::presentation
