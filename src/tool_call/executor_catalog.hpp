#pragma once

#include "core_types/chat_ids.hpp"
#include "tool_call/executor.hpp"

#include <vector>

namespace yac::tool_call {

[[nodiscard]] std::vector<chat::ToolDefinition> ToolDefinitions();
[[nodiscard]] PreparedToolCall PrepareToolCall(
    const chat::ToolCallRequest& request);
void ValidateToolNames(const std::vector<chat::ToolDefinition>& tools);

}  // namespace yac::tool_call
