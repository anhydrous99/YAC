#include "tool_call/executor.hpp"

#include "tool_call/executor_arguments.hpp"
#include "tool_call/executor_catalog.hpp"
#include "tool_call/filesystem_tool_executor.hpp"
#include "tool_call/lsp_tool_executor.hpp"
#include "tool_call/sub_agent_tool_executor.hpp"

#include <stdexcept>
#include <utility>

namespace yac::tool_call {

namespace {

ToolExecutionResult ErrorResult(ToolCallBlock block, std::string message) {
  std::visit(
      [&message](auto& call) {
        if constexpr (requires {
                        call.is_error;
                        call.error;
                      }) {
          call.is_error = true;
          call.error = message;
        }
      },
      block);
  return ToolExecutionResult{
      .block = std::move(block),
      .result_json = Json{{"error", message}}.dump(),
      .is_error = true,
  };
}

}  // namespace

ToolExecutor::ToolExecutor(std::filesystem::path workspace_root,
                           std::shared_ptr<ILspClient> lsp_client)
    : workspace_filesystem_(std::move(workspace_root)),
      lsp_client_(std::move(lsp_client)) {}

void ToolExecutor::SetSubAgentManager(chat::SubAgentManager* manager) {
  sub_agent_manager_ = manager;
}

std::vector<chat::ToolDefinition> ToolExecutor::Definitions() {
  return ToolDefinitions();
}

PreparedToolCall ToolExecutor::Prepare(const chat::ToolCallRequest& request) {
  return PrepareToolCall(request);
}

ToolExecutionResult ToolExecutor::Execute(const PreparedToolCall& prepared,
                                          std::stop_token stop_token) const {
  if (stop_token.stop_requested()) {
    return ErrorResult(prepared.preview, "Tool execution cancelled.");
  }
  try {
    if (prepared.request.name == kFileReadToolName) {
      return ExecuteFileReadTool(prepared.request, workspace_filesystem_);
    }
    if (prepared.request.name == kFileWriteToolName) {
      return ExecuteFileWriteTool(prepared.request, workspace_filesystem_);
    }
    if (prepared.request.name == kListDirToolName) {
      return ExecuteListDirTool(prepared.request, workspace_filesystem_);
    }
    if (prepared.request.name == kLspDiagnosticsToolName) {
      return ExecuteLspDiagnosticsTool(prepared.request, *lsp_client_,
                                       workspace_filesystem_);
    }
    if (prepared.request.name == kLspReferencesToolName) {
      return ExecuteLspReferencesTool(prepared.request, *lsp_client_,
                                      workspace_filesystem_);
    }
    if (prepared.request.name == kLspGotoDefinitionToolName) {
      return ExecuteLspGotoDefinitionTool(prepared.request, *lsp_client_,
                                          workspace_filesystem_);
    }
    if (prepared.request.name == kLspRenameToolName) {
      return ExecuteLspRenameTool(prepared.request, *lsp_client_,
                                  workspace_filesystem_);
    }
    if (prepared.request.name == kLspSymbolsToolName) {
      return ExecuteLspSymbolsTool(prepared.request, *lsp_client_,
                                   workspace_filesystem_);
    }
    if (prepared.request.name == kSubAgentToolName) {
      return ExecuteSubAgentTool(prepared, sub_agent_manager_, stop_token);
    }
    if (prepared.request.name == kTodoWriteToolName) {
      return ToolExecutionResult{
          .block = prepared.preview,
          .result_json = R"({"result":"[todo_write: not yet implemented]"})",
      };
    }
    if (prepared.request.name == kAskUserToolName) {
      return ToolExecutionResult{
          .block = prepared.preview,
          .result_json = R"({"result":"[ask_user: not yet implemented]"})",
      };
    }
    return ErrorResult(prepared.preview,
                       "Unknown tool: " + prepared.request.name);
  } catch (const std::exception& error) {
    return ErrorResult(prepared.preview, error.what());
  }
}

}  // namespace yac::tool_call
