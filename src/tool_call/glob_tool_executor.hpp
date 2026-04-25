#pragma once

#include "chat/types.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/workspace_filesystem.hpp"

namespace yac::tool_call {

[[nodiscard]] ToolExecutionResult ExecuteGlobTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem);

}  // namespace yac::tool_call
