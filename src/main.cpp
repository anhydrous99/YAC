#include "app/chat_event_bridge.hpp"
#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/slash_command_registry.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/provider_registry.hpp"

#include <memory>
#include <string>
#include <utility>

#include <ftxui/component/app.hpp>

int main() {
  auto config = yac::chat::LoadChatConfigFromEnv();

  yac::provider::ProviderRegistry registry;
  registry.Register(std::make_shared<yac::provider::OpenAiChatProvider>(
      yac::chat::ProviderConfig{
          .id = config.provider_id,
          .model = config.model,
          .api_key = config.api_key,
          .api_key_env = config.api_key_env,
          .base_url = config.base_url,
      }));
  yac::chat::ChatService chat_service(std::move(registry), config);

  yac::presentation::ChatUI chat_ui;
  auto screen = ftxui::App::Fullscreen();

  yac::app::ChatEventBridge bridge(chat_ui);

  chat_service.SetEventCallback([&bridge, &screen](yac::chat::ChatEvent event) {
    screen.Post([&bridge, &screen, event = std::move(event)] {
      bridge.HandleEvent(event);
      screen.PostEvent(ftxui::Event::Custom);
    });
  });

  chat_ui.SetOnSend([&chat_service](const std::string& message) {
    chat_service.SubmitUserMessage(message);
  });

  chat_ui.SetOnCommand([&chat_service](const std::string& command) {
    if (command == "new_chat" || command == "clear_messages") {
      chat_service.ResetConversation();
    } else if (command == "cancel_response") {
      chat_service.CancelActiveResponse();
    }
  });

  chat_ui.SetCommands({
      {"new_chat", "New Chat", "Start a fresh conversation"},
      {"clear_messages", "Clear Messages", "Remove all messages from the view"},
      {"cancel_response", "Cancel Response",
       "Stop the current assistant response"},
  });

  auto exit_loop = screen.ExitLoopClosure();
  yac::presentation::SlashCommandRegistry slash_registry;
  yac::presentation::RegisterBuiltinSlashCommands(slash_registry);
  slash_registry.SetHandler("quit", exit_loop);
  chat_ui.SetSlashCommands(std::move(slash_registry));

  auto component = chat_ui.Build();
  screen.Loop(component);

  return 0;
}
