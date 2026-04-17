#pragma once

#include "tool_call/executor.hpp"
#include "tool_call/workspace_filesystem.hpp"

namespace yac::tool_call {

[[nodiscard]] ToolExecutionResult ExecuteFileWriteTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem);
[[nodiscard]] ToolExecutionResult ExecuteListDirTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem);

}  // namespace yac::tool_call
