#pragma once

#include <string>
#include <vector>

namespace yac::presentation {

struct SlashCommand;

class ComposerState {
 public:
  [[nodiscard]] bool Empty() const;
  [[nodiscard]] int CalculateHeight(int max_lines) const;

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

 private:
  std::string content_;
  int cursor_ = 0;
  bool slash_menu_active_ = false;
  int slash_menu_selected_ = 0;
};

}  // namespace yac::presentation
