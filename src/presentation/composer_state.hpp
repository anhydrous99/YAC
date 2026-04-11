#pragma once

#include <string>

namespace yac::presentation {

class ComposerState {
 public:
  [[nodiscard]] bool Empty() const;
  [[nodiscard]] int CalculateHeight(int max_lines) const;

  [[nodiscard]] std::string& Content();
  [[nodiscard]] const std::string& Content() const;
  [[nodiscard]] int* CursorPosition();

  void InsertNewline();
  [[nodiscard]] std::string Submit();

 private:
  std::string content_;
  int cursor_ = 0;
};

}  // namespace yac::presentation
