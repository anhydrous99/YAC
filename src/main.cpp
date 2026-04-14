#include "app/chat_event_bridge.hpp"
#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/slash_command_registry.hpp"
#include "provider/openai_chat_provider.hpp"
#include "provider/provider_registry.hpp"

#include <chrono>
#include <exception>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <ftxui/component/app.hpp>

namespace {

constexpr std::string_view kSwitchModelPrefix = "switch_model:";
constexpr std::string_view kSwitchModelCommandId = "switch_model";

std::vector<yac::chat::ModelInfo> ZaiFallbackModels() {
  return {
      {.id = "glm-5.1", .display_name = "glm-5.1"},
      {.id = "glm-5", .display_name = "glm-5"},
      {.id = "glm-4.7", .display_name = "glm-4.7"},
      {.id = "glm-4.6", .display_name = "glm-4.6"},
      {.id = "glm-4.5", .display_name = "glm-4.5"},
      {.id = "glm-4.5-air", .display_name = "glm-4.5-air"},
  };
}

void EnsureModelPresent(std::vector<yac::chat::ModelInfo>& models,
                        const std::string& model_id) {
  if (model_id.empty()) {
    return;
  }
  for (const auto& model : models) {
    if (model.id == model_id) {
      return;
    }
  }
  models.insert(models.begin(),
                yac::chat::ModelInfo{.id = model_id, .display_name = model_id});
}

std::vector<yac::chat::ModelInfo> DiscoverModels(
    yac::provider::LanguageModelProvider& provider,
    const yac::chat::ChatConfig& config) {
  std::vector<yac::chat::ModelInfo> models;
  if (config.provider_id != "zai") {
    return models;
  }

  try {
    models = provider.ListModels(std::chrono::seconds(5));
  } catch (const std::exception&) {
    models.clear();
  }
  if (models.empty()) {
    models = ZaiFallbackModels();
  }
  EnsureModelPresent(models, config.model);
  return models;
}

std::vector<yac::presentation::Command> BuildCommands(
    const std::vector<yac::chat::ModelInfo>& models) {
  std::vector<yac::presentation::Command> commands{
      {"new_chat", "New Chat", "Start a fresh conversation"},
      {"clear_messages", "Clear Messages", "Remove all messages from the view"},
      {"cancel_response", "Cancel Response",
       "Stop the current assistant response"},
  };

  if (!models.empty()) {
    commands.emplace_back(std::string(kSwitchModelCommandId), "Switch Model",
                          "Choose the model for future responses");
  }

  return commands;
}

std::vector<yac::presentation::Command> BuildModelCommands(
    const std::vector<yac::chat::ModelInfo>& models) {
  std::vector<yac::presentation::Command> commands;
  for (const auto& model : models) {
    commands.emplace_back(std::string(kSwitchModelPrefix) + model.id,
                          model.display_name,
                          "Use " + model.id + " for future responses");
  }
  return commands;
}

bool StartsWith(std::string_view value, std::string_view prefix) {
  return value.size() >= prefix.size() &&
         value.substr(0, prefix.size()) == prefix;
}

}  // namespace

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
  auto models = DiscoverModels(*provider, config);

  yac::provider::ProviderRegistry registry;
  registry.Register(std::move(provider));
  yac::chat::ChatService chat_service(std::move(registry), config);

  yac::presentation::ChatUI chat_ui;
  chat_ui.SetProviderModel(config.provider_id, config.model);
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
    } else if (StartsWith(command, kSwitchModelPrefix)) {
      chat_service.SetModel(command.substr(kSwitchModelPrefix.size()));
    }
  });

  chat_ui.SetCommands(BuildCommands(models));
  chat_ui.SetModelCommands(BuildModelCommands(models));

  auto exit_loop = screen.ExitLoopClosure();
  yac::presentation::SlashCommandRegistry slash_registry;
  yac::presentation::RegisterBuiltinSlashCommands(slash_registry);
  slash_registry.SetHandler("quit", exit_loop);
  chat_ui.SetSlashCommands(std::move(slash_registry));

  auto component = chat_ui.Build();
  screen.Loop(component);

  return 0;
}
