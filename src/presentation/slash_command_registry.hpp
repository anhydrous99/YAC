#pragma once

#include <functional>
#include <string>
#include <vector>

namespace yac::presentation {

struct SlashCommand {
  std::string name;
  std::string description;
  std::function<void()> handler;
};

class SlashCommandRegistry {
 public:
  void Register(SlashCommand command);

  [[nodiscard]] bool TryDispatch(const std::string& input) const;
  [[nodiscard]] const std::vector<SlashCommand>& Commands() const;

 private:
  static std::string ExtractCommandName(const std::string& input);

  std::vector<SlashCommand> commands_;
};

}  // namespace yac::presentation
