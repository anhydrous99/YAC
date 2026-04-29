#pragma once

#include <memory>

namespace ftxui {
class App;
}

namespace yac {

namespace cli {
class McpAdminCommand;
}

namespace presentation {
class ChatUI;
class SlashCommandRegistry;
}  // namespace presentation

namespace app {

void HandleMcpListCommand(
    presentation::ChatUI& chat_ui,
    const std::shared_ptr<cli::McpAdminCommand>& mcp_admin);
void ShowMcpAddUsage(presentation::ChatUI& chat_ui);
void RegisterMcpSlashCommandHandlers(
    presentation::SlashCommandRegistry& slash_registry,
    presentation::ChatUI& chat_ui, ftxui::App& screen,
    std::shared_ptr<cli::McpAdminCommand> mcp_admin);

}  // namespace app
}  // namespace yac
