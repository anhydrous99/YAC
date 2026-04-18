#pragma once

#include "tool_call/executor.hpp"

#include <stop_token>

namespace yac::chat {
class SubAgentManager;
}  // namespace yac::chat

namespace yac::tool_call {

[[nodiscard]] ToolExecutionResult ExecuteSubAgentTool(
    const PreparedToolCall& prepared, chat::SubAgentManager* sub_agent_manager,
    std::stop_token stop_token = {});

}  // namespace yac::tool_call
