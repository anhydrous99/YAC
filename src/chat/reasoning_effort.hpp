#pragma once

#include "chat/types.hpp"

#include <optional>
#include <string_view>
#include <vector>

namespace yac::chat {

[[nodiscard]] std::optional<ReasoningEffort> ParseReasoningEffortValue(
    std::string_view reasoning_effort);
[[nodiscard]] std::string_view ReasoningEffortToString(
    ReasoningEffort reasoning_effort);
[[nodiscard]] std::vector<ReasoningEffort> AllReasoningEffortValuesList();

}  // namespace yac::chat
