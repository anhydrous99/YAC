#pragma once

#include "chat/types.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <stop_token>

namespace yac::tool_call {

[[nodiscard]] ToolExecutionResult ExecuteGrepTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem,
    std::stop_token stop_token);

}  // namespace yac::tool_call
