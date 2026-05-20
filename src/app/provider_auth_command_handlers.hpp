#pragma once

#include <memory>

namespace ftxui {
class App;
}

namespace yac {

namespace cli {
class ProviderAuthCommand;
}

namespace presentation {
class ChatUI;
class SlashCommandRegistry;
}  // namespace presentation

namespace app {

void RegisterProviderAuthSlashCommandHandlers(
    presentation::SlashCommandRegistry& slash_registry,
    presentation::ChatUI& chat_ui, ftxui::App& screen,
    std::shared_ptr<cli::ProviderAuthCommand> provider_auth_command);

}  // namespace app
}  // namespace yac
