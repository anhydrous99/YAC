#include "provider/reasoning_effort_capability.hpp"

#include "chat/reasoning_effort.hpp"

#include <string>
#include <unordered_map>
#include <utility>

namespace yac::provider {
namespace {

using EffortTable =
    std::unordered_map<std::string, std::vector<chat::ReasoningEffort>>;

std::string TrimTrailingSlash(std::string value) {
  while (!value.empty() && value.back() == '/') {
    value.pop_back();
  }
  return value;
}

const EffortTable& SupportedModels() {
  static const EffortTable table = {
      {"gpt-5-pro", {chat::ReasoningEffort::High}},
      {"gpt-5.5",
       {chat::ReasoningEffort::None, chat::ReasoningEffort::Minimal,
        chat::ReasoningEffort::Low, chat::ReasoningEffort::Medium,
        chat::ReasoningEffort::High, chat::ReasoningEffort::XHigh}},
      {"gpt-5.3-codex",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High,
        chat::ReasoningEffort::XHigh}},
      {"gpt-5.3-codex-spark",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High,
        chat::ReasoningEffort::XHigh}},
      {"codex-mini-latest",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High,
        chat::ReasoningEffort::XHigh}},
      {"gpt-5",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High}},
      {"gpt-5-mini",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High}},
      {"gpt-5-nano",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High}},
      {"gpt-5.1",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High}},
      {"gpt-5.2",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High}},
      {"gpt-5.4",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High}},
      {"gpt-5.4-mini",
       {chat::ReasoningEffort::Minimal, chat::ReasoningEffort::Low,
        chat::ReasoningEffort::Medium, chat::ReasoningEffort::High}},
      {"o1",
       {chat::ReasoningEffort::Low, chat::ReasoningEffort::Medium,
        chat::ReasoningEffort::High}},
      {"o1-mini",
       {chat::ReasoningEffort::Low, chat::ReasoningEffort::Medium,
        chat::ReasoningEffort::High}},
      {"o3",
       {chat::ReasoningEffort::Low, chat::ReasoningEffort::Medium,
        chat::ReasoningEffort::High}},
      {"o3-mini",
       {chat::ReasoningEffort::Low, chat::ReasoningEffort::Medium,
        chat::ReasoningEffort::High}},
      {"o4",
       {chat::ReasoningEffort::Low, chat::ReasoningEffort::Medium,
        chat::ReasoningEffort::High}},
      {"o4-mini",
       {chat::ReasoningEffort::Low, chat::ReasoningEffort::Medium,
        chat::ReasoningEffort::High}},
  };
  return table;
}

std::optional<ReasoningEffortCapability> MakeCapability(
    const std::vector<chat::ReasoningEffort>& allowed_values,
    std::vector<ReasoningEffortProtocol> protocols) {
  return ReasoningEffortCapability{.allowed_values = allowed_values,
                                   .protocols = std::move(protocols)};
}

bool IsOpenAiProvider(const chat::ProviderConfig& config) {
  return config.id.value == "openai";
}

bool IsOpenAiCompatibleProvider(const chat::ProviderConfig& config) {
  return config.id.value == "openai-compatible";
}

std::optional<std::vector<ReasoningEffortProtocol>> SupportedProtocols(
    const chat::ProviderConfig& config) {
  if (IsOpenAiProvider(config)) {
    return std::vector<ReasoningEffortProtocol>{
        ReasoningEffortProtocol::OpenAiResponses,
        ReasoningEffortProtocol::OpenAiChatCompletions};
  }
  if (!IsOpenAiCompatibleProvider(config)) {
    return std::nullopt;
  }
  if (TrimTrailingSlash(config.base_url) != "https://api.openai.com/v1") {
    return std::nullopt;
  }
  return std::vector<ReasoningEffortProtocol>{
      ReasoningEffortProtocol::OpenAiChatCompletions};
}

}  // namespace

std::optional<chat::ReasoningEffort> ParseReasoningEffort(
    std::string_view reasoning_effort) {
  return chat::ParseReasoningEffortValue(reasoning_effort);
}

std::string_view ToString(chat::ReasoningEffort reasoning_effort) {
  return chat::ReasoningEffortToString(reasoning_effort);
}

std::vector<chat::ReasoningEffort> AllReasoningEffortValues() {
  return chat::AllReasoningEffortValuesList();
}

std::optional<ReasoningEffortCapability> LookupReasoningEffortCapability(
    const chat::ProviderConfig& config, std::string_view model_id) {
  const auto protocols = SupportedProtocols(config);
  if (!protocols.has_value() || model_id.empty()) {
    return std::nullopt;
  }
  const auto it = SupportedModels().find(std::string(model_id));
  if (it == SupportedModels().end()) {
    return std::nullopt;
  }
  return MakeCapability(it->second, std::move(*protocols));
}

}  // namespace yac::provider
