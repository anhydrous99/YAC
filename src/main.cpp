#include "app/chat_event_bridge.hpp"
#include "app/model_discovery.hpp"
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

using namespace yac::presentation;

int main() {
  auto config = yac::chat::LoadChatConfigFromEnv();

  auto provider = std::make_shared<yac::provider::OpenAiChatProvider>(
      yac::chat::ProviderConfig{
          .id = config.provider_id,
          .model = config.model,
          .api_key = config.api_key,
          .api_key_env = config.api_key_env,
          .base_url = config.base_url,
      });
  auto models = yac::app::DiscoverModels(*provider, config);

  yac::provider::ProviderRegistry registry;
  registry.Register(std::move(provider));
  yac::chat::ChatService chat_service(std::move(registry), config);

  auto screen = ftxui::App::Fullscreen();
  yac::presentation::ChatUI chat_ui;
  chat_ui.SetUiTaskRunner([&screen](yac::presentation::ChatUI::UiTask task) {
    screen.Post([&screen, task = std::move(task)]() mutable {
      task();
      screen.PostEvent(ftxui::Event::Custom);
    });
  });
  chat_ui.SetProviderModel(config.provider_id, config.model);

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
    } else if (command.starts_with(kSwitchModelPrefix)) {
      chat_service.SetModel(command.substr(kSwitchModelPrefix.size()));
    }
  });

  chat_ui.SetCommands(yac::app::BuildCommands(models));
  chat_ui.SetModelCommands(yac::app::BuildModelCommands(models));

  auto exit_loop = screen.ExitLoopClosure();
  yac::presentation::SlashCommandRegistry slash_registry;
  yac::presentation::RegisterBuiltinSlashCommands(slash_registry);
  slash_registry.SetHandler("quit", exit_loop);
  slash_registry.SetHandler(
      "clear", [&chat_service] { chat_service.ResetConversation(); });
  chat_ui.SetSlashCommands(std::move(slash_registry));

  auto component = chat_ui.Build();
  screen.Loop(component);

  return 0;
}
