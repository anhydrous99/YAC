#include "tool_call/executor.hpp"

#include "tool_call/executor_arguments.hpp"
#include "tool_call/executor_catalog.hpp"
#include "tool_call/filesystem_tool_executor.hpp"
#include "tool_call/lsp_tool_executor.hpp"

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
    if (prepared.request.name == "file_read") {
      return ExecuteFileReadTool(prepared.request, workspace_filesystem_);
    }
    if (prepared.request.name == "file_write") {
      return ExecuteFileWriteTool(prepared.request, workspace_filesystem_);
    }
    if (prepared.request.name == "list_dir") {
      return ExecuteListDirTool(prepared.request, workspace_filesystem_);
    }
    if (prepared.request.name == "lsp_diagnostics") {
      return ExecuteLspDiagnosticsTool(prepared.request, *lsp_client_,
                                       workspace_filesystem_);
    }
    if (prepared.request.name == "lsp_references") {
      return ExecuteLspReferencesTool(prepared.request, *lsp_client_,
                                      workspace_filesystem_);
    }
    if (prepared.request.name == "lsp_goto_definition") {
      return ExecuteLspGotoDefinitionTool(prepared.request, *lsp_client_,
                                          workspace_filesystem_);
    }
    if (prepared.request.name == "lsp_rename") {
      return ExecuteLspRenameTool(prepared.request, *lsp_client_,
                                  workspace_filesystem_);
    }
    if (prepared.request.name == "lsp_symbols") {
      return ExecuteLspSymbolsTool(prepared.request, *lsp_client_,
                                   workspace_filesystem_);
    }
    return ErrorResult(prepared.preview,
                       "Unknown tool: " + prepared.request.name);
  } catch (const std::exception& error) {
    return ErrorResult(prepared.preview, error.what());
  }
}

}  // namespace yac::tool_call
