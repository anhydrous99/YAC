#pragma once

#include "tool_call/executor.hpp"

namespace yac::tool_call {

[[nodiscard]] std::vector<chat::ToolDefinition> ToolDefinitions();
[[nodiscard]] PreparedToolCall PrepareToolCall(
    const chat::ToolCallRequest& request);

}  // namespace yac::tool_call
