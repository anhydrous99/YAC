#pragma once

#include "core_types/chat_ids.hpp"
#include "tool_call/executor.hpp"

#include <vector>

namespace yac::tool_call {

[[nodiscard]] std::vector<chat::ToolDefinition> ToolDefinitions();
[[nodiscard]] PreparedToolCall PrepareToolCall(
    const chat::ToolCallRequest& request);

}  // namespace yac::tool_call
