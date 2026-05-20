#include "presentation/provider_auth_slash_commands.hpp"

namespace yac::presentation {

void RegisterProviderAuthSlashCommands(SlashCommandRegistry& registry) {
  registry.Define(
      "auth", "auth",
      "Manage provider auth (openai login|status|logout|set-api-key)");
}

}  // namespace yac::presentation
