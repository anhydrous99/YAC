#pragma once

#include <string>

namespace yac::chat {
struct ChatConfig;
class ChatService;
}  // namespace yac::chat

namespace yac::presentation {
class ChatUI;
class SlashCommandRegistry;
struct StartupStatus;
}  // namespace yac::presentation

namespace yac::app {

int RunApp();
std::string BuildHelpText(const presentation::StartupStatus& startup,
                          const chat::ChatConfig& config);
void RegisterEffortSlashCommandHandlers(
    presentation::SlashCommandRegistry& slash_registry,
    chat::ChatService& chat_service, presentation::ChatUI& chat_ui);

}  // namespace yac::app
