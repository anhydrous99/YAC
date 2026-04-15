#include "app/model_discovery.hpp"

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

std::vector<chat::ModelInfo> DiscoverModels(
    provider::LanguageModelProvider& provider, const chat::ChatConfig& config) {
  std::vector<chat::ModelInfo> models;
  if (!provider.SupportsModelDiscovery()) {
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

std::vector<Command> BuildCommands(const std::vector<chat::ModelInfo>& models) {
  std::vector<Command> commands{
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
