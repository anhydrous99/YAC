#include "app/model_discovery.hpp"

#include "presentation/theme.hpp"

#include <chrono>
#include <exception>
#include <string>
#include <utility>
#include <vector>

namespace yac::app {
namespace {

using namespace yac::presentation;

std::vector<chat::ModelInfo> ZaiFallbackModels() {
  return {
      {.id = "glm-5.1", .display_name = "glm-5.1"},
      {.id = "glm-5", .display_name = "glm-5"},
      {.id = "glm-4.7", .display_name = "glm-4.7"},
      {.id = "glm-4.6", .display_name = "glm-4.6"},
      {.id = "glm-4.5", .display_name = "glm-4.5"},
      {.id = "glm-4.5-air", .display_name = "glm-4.5-air"},
  };
}

bool UsesZaiFallback(const provider::LanguageModelProvider& provider,
                     const chat::ChatConfig& config) {
  return provider.Id() == "zai" || config.provider_id == "zai";
}

void EnsureModelPresent(std::vector<chat::ModelInfo>& models,
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
                chat::ModelInfo{.id = model_id, .display_name = model_id});
}

}  // namespace

ModelDiscoveryResult DiscoverModelsWithStatus(
    provider::LanguageModelProvider& provider, const chat::ChatConfig& config) {
  if (!provider.SupportsModelDiscovery()) {
    return {.status = ModelDiscoveryStatus::Unsupported,
            .message = "Model discovery is not supported by this provider."};
  }

  ModelDiscoveryResult result{.status = ModelDiscoveryStatus::Success};
  try {
    result.models = provider.ListModels(std::chrono::seconds(5));
  } catch (const std::exception& error) {
    result.status = ModelDiscoveryStatus::Failed;
    result.message = error.what();
    result.models.clear();
  }
  if (result.models.empty() && UsesZaiFallback(provider, config)) {
    result.models = ZaiFallbackModels();
    result.status = ModelDiscoveryStatus::Fallback;
    if (result.message.empty()) {
      result.message = "Using built-in Z.ai model list.";
    } else {
      result.message =
          "Model discovery failed; using built-in Z.ai model list.";
    }
  }
  EnsureModelPresent(result.models, config.model);
  if (result.models.empty() && result.status == ModelDiscoveryStatus::Success) {
    result.status = ModelDiscoveryStatus::Failed;
    result.message = "No models were returned by the provider.";
  }
  return result;
}

std::vector<chat::ModelInfo> DiscoverModels(
    provider::LanguageModelProvider& provider, const chat::ChatConfig& config) {
  return DiscoverModelsWithStatus(provider, config).models;
}

std::vector<Command> BuildCommands(const std::vector<chat::ModelInfo>& models) {
  std::vector<Command> commands{
      {"new_chat", "New Chat", "Start a fresh conversation"},
      {"clear_messages", "Clear Messages", "Remove all messages from the view"},
      {"cancel_response", "Cancel Response",
       "Stop the current assistant response"},
      {"help", "Help", "Show shortcuts, setup, and workspace status"},
      {std::string(kSwitchThemeCommandId), "Switch Theme",
       "Choose a color theme"},
  };

  if (!models.empty()) {
    commands.emplace_back(std::string(kSwitchModelCommandId), "Switch Model",
                          "Choose the model for future responses");
  }

  return commands;
}

std::vector<Command> BuildThemeCommands() {
  std::vector<Command> commands;
  for (const auto& name : theme::ListThemes()) {
    commands.emplace_back(std::string(kSwitchThemePrefix) + name, name,
                          "Apply the " + name + " theme");
  }
  return commands;
}

std::vector<Command> BuildModelCommands(
    const std::vector<chat::ModelInfo>& models) {
  std::vector<Command> commands;
  for (const auto& model : models) {
    commands.emplace_back(std::string(kSwitchModelPrefix) + model.id,
                          model.display_name,
                          "Use " + model.id + " for future responses");
  }
  return commands;
}

}  // namespace yac::app
