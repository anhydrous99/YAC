#pragma once

#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace yac::presentation {

struct SlashCommand {
  std::string id;
  std::string name;
  std::string description;
  std::vector<std::string> aliases;
  std::optional<std::function<void()>> handler;
  std::optional<std::function<void(std::string)>> arguments_handler;
};

class SlashCommandRegistry {
 public:
  void Define(std::string id, std::string name, std::string description,
              std::vector<std::string> aliases = {});
  void SetHandler(const std::string& id, std::function<void()> handler);
  void SetArgumentsHandler(const std::string& id,
                           std::function<void(std::string)> handler);

  // Legacy API for backwards compatibility with tests.
  void Register(SlashCommand command);

  [[nodiscard]] bool TryDispatch(const std::string& input) const;
  [[nodiscard]] const std::vector<SlashCommand>& Commands() const;

 private:
  static std::string ExtractCommandName(const std::string& input);

  std::vector<SlashCommand> commands_;
};

void RegisterBuiltinSlashCommands(SlashCommandRegistry& registry);

}  // namespace yac::presentation
