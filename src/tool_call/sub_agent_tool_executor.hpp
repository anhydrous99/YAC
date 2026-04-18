#pragma once

#include "tool_call/executor.hpp"

namespace yac::chat {
class SubAgentManager;
}  // namespace yac::chat

namespace yac::tool_call {

[[nodiscard]] ToolExecutionResult ExecuteSubAgentTool(
    const PreparedToolCall& prepared,
    chat::SubAgentManager* sub_agent_manager);

}  // namespace yac::tool_call
