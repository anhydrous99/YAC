#pragma once

#include "chat/types.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace yac::provider {

enum class ReasoningEffortProtocol {
  OpenAiResponses,
  OpenAiChatCompletions,
};

struct ReasoningEffortCapability {
  std::vector<chat::ReasoningEffort> allowed_values;
  std::vector<ReasoningEffortProtocol> protocols;
};

[[nodiscard]] std::optional<chat::ReasoningEffort> ParseReasoningEffort(
    std::string_view reasoning_effort);
[[nodiscard]] std::string_view ToString(chat::ReasoningEffort reasoning_effort);
[[nodiscard]] std::vector<chat::ReasoningEffort> AllReasoningEffortValues();
[[nodiscard]] std::optional<ReasoningEffortCapability>
LookupReasoningEffortCapability(const chat::ProviderConfig& config,
                                std::string_view model_id);

}  // namespace yac::provider
