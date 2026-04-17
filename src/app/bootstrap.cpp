#include "app/bootstrap.hpp"

#include "app/chat_event_bridge.hpp"
#include "app/model_context_windows.hpp"
#include "app/model_discovery.hpp"
#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/slash_command_registry.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/provider_registry.hpp"

#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <ftxui/component/app.hpp>

namespace yac::app {
namespace {

std::shared_ptr<provider::OpenAiChatProvider> BuildProvider(
    const chat::ChatConfig& config) {
  return std::make_shared<provider::OpenAiChatProvider>(chat::ProviderConfig{
      .id = config.provider_id,
      .model = config.model,
      .api_key = config.api_key,
      .api_key_env = config.api_key_env,
      .base_url = config.base_url,
  });
}

void ConfigureUiTaskRunner(ftxui::App& screen, presentation::ChatUI& chat_ui) {
  chat_ui.SetUiTaskRunner([&screen](presentation::ChatUI::UiTask task) {
    screen.Post([&screen, task = std::move(task)]() mutable {
      task();
      screen.PostEvent(ftxui::Event::Custom);
    });
  });
}

void ConfigureServiceEventCallback(ftxui::App& screen, ChatEventBridge& bridge,
                                   chat::ChatService& chat_service) {
  chat_service.SetEventCallback([&bridge, &screen](chat::ChatEvent event) {
    screen.Post([&bridge, &screen, event = std::move(event)] {
      bridge.HandleEvent(event);
      screen.PostEvent(ftxui::Event::Custom);
    });
  });
}

void ConfigureChatUiCallbacks(const std::vector<chat::ModelInfo>& models,
                              chat::ChatService& chat_service,
                              presentation::ChatUI& chat_ui) {
  chat_ui.SetOnSend([&chat_service](const std::string& message) {
    chat_service.SubmitUserMessage(message);
  });

  chat_ui.SetOnCommand([&chat_service](const std::string& command) {
    if (command == "new_chat" || command == "clear_messages") {
      chat_service.ResetConversation();
    } else if (command == "cancel_response") {
      chat_service.CancelActiveResponse();
    } else if (command.starts_with(presentation::kSwitchModelPrefix)) {
      chat_service.SetModel(
          command.substr(std::string(presentation::kSwitchModelPrefix).size()));
    }
  });
  chat_ui.SetOnToolApproval(
      [&chat_service](const std::string& approval_id, bool approved) {
        chat_service.ResolveToolApproval(approval_id, approved);
      });

  chat_ui.SetCommands(BuildCommands(models));
  chat_ui.SetModelCommands(BuildModelCommands(models));
}

presentation::SlashCommandRegistry BuildSlashCommandRegistry(
    std::function<void()> exit_loop, chat::ChatService& chat_service) {
  presentation::SlashCommandRegistry slash_registry;
  presentation::RegisterBuiltinSlashCommands(slash_registry);
  slash_registry.SetHandler("quit", std::move(exit_loop));
  slash_registry.SetHandler(
      "clear", [&chat_service] { chat_service.ResetConversation(); });
  return slash_registry;
}

}  // namespace

int RunApp() {
  auto config = chat::LoadChatConfigFromEnv();
  auto provider = BuildProvider(config);
  auto models = DiscoverModels(*provider, config);

  auto screen = ftxui::App::Fullscreen();
  presentation::ChatUI chat_ui;
  ConfigureUiTaskRunner(screen, chat_ui);
  chat_ui.SetContextWindowTokens(LookupContextWindow(config.model));
  chat_ui.SetProviderModel(config.provider_id, config.model);

  ChatEventBridge bridge(chat_ui);

  provider::ProviderRegistry registry;
  registry.Register(provider);
  chat::ChatService chat_service(std::move(registry), config);

  ConfigureServiceEventCallback(screen, bridge, chat_service);
  ConfigureChatUiCallbacks(models, chat_service, chat_ui);

  chat_ui.SetSlashCommands(
      BuildSlashCommandRegistry(screen.ExitLoopClosure(), chat_service));

  auto component = chat_ui.Build();
  screen.Loop(component);
  return 0;
}

}  // namespace yac::app
