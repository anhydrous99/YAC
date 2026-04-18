#pragma once

#include "chat/model_info.hpp"
#include "chat/types.hpp"
#include "presentation/command_palette.hpp"
#include "provider/language_model_provider.hpp"

#include <string>
#include <vector>

namespace yac::app {

enum class ModelDiscoveryStatus { Unsupported, Success, Fallback, Failed };

struct ModelDiscoveryResult {
  std::vector<chat::ModelInfo> models;
  ModelDiscoveryStatus status = ModelDiscoveryStatus::Unsupported;
  std::string message;
};

[[nodiscard]] ModelDiscoveryResult DiscoverModelsWithStatus(
    provider::LanguageModelProvider& provider, const chat::ChatConfig& config);

[[nodiscard]] std::vector<chat::ModelInfo> DiscoverModels(
    provider::LanguageModelProvider& provider, const chat::ChatConfig& config);

[[nodiscard]] std::vector<presentation::Command> BuildCommands(
    const std::vector<chat::ModelInfo>& models);

[[nodiscard]] std::vector<presentation::Command> BuildModelCommands(
    const std::vector<chat::ModelInfo>& models);

}  // namespace yac::app
