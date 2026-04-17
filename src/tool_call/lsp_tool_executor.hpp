#pragma once

#include "tool_call/executor.hpp"
#include "tool_call/workspace_filesystem.hpp"

namespace yac::tool_call {

[[nodiscard]] ToolExecutionResult ExecuteLspDiagnosticsTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem);
[[nodiscard]] ToolExecutionResult ExecuteLspReferencesTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem);
[[nodiscard]] ToolExecutionResult ExecuteLspGotoDefinitionTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem);
[[nodiscard]] ToolExecutionResult ExecuteLspRenameTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem);
[[nodiscard]] ToolExecutionResult ExecuteLspSymbolsTool(
    const chat::ToolCallRequest& request, ILspClient& lsp_client,
    const WorkspaceFilesystem& workspace_filesystem);

}  // namespace yac::tool_call
