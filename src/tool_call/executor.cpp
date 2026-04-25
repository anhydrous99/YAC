#include "tool_call/executor.hpp"

#include "chat/chat_service_tool_approval.hpp"
#include "tool_call/bash_tool_executor.hpp"
#include "tool_call/edit_tool_executor.hpp"
#include "tool_call/executor_arguments.hpp"
#include "tool_call/executor_catalog.hpp"
#include "tool_call/filesystem_tool_executor.hpp"
#include "tool_call/glob_tool_executor.hpp"
#include "tool_call/grep_tool_executor.hpp"
#include "tool_call/lsp_tool_executor.hpp"
#include "tool_call/sub_agent_tool_executor.hpp"
#include "tool_call/todo_state.hpp"

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
                           std::shared_ptr<ILspClient> lsp_client,
                           TodoState& todo_state)
    : workspace_filesystem_(std::move(workspace_root)),
      lsp_client_(std::move(lsp_client)),
      todo_state_(todo_state) {}

void ToolExecutor::SetSubAgentManager(chat::SubAgentManager* manager) {
  sub_agent_manager_ = manager;
}

void ToolExecutor::SetToolApproval(
    chat::internal::ChatServiceToolApproval* tool_approval) {
  tool_approval_ = tool_approval;
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
    if (prepared.request.name == kBashToolName) {
      return ExecuteBashTool(prepared.request, workspace_filesystem_.Root(),
                             stop_token);
    }
    if (prepared.request.name == kTodoWriteToolName) {
      const auto& call = std::get<TodoWriteCall>(prepared.preview);
      todo_state_.Update(call.todos);
      auto current = todo_state_.Current();
      Json todos_json = Json::array();
      for (const auto& item : current) {
        todos_json.push_back({{"content", item.content},
                              {"status", item.status},
                              {"priority", item.priority}});
      }
      return ToolExecutionResult{
          .block = prepared.preview,
          .result_json = Json{{"todos", std::move(todos_json)}}.dump(),
      };
    }
    if (prepared.request.name == kAskUserToolName) {
      auto call = std::get<AskUserCall>(prepared.preview);
      if (tool_approval_ == nullptr || prepared.approval_id.empty()) {
        constexpr auto kAskUserPipelineError =
            "Ask user approval pipeline unavailable.";
        call.is_error = true;
        call.error = kAskUserPipelineError;
        return ToolExecutionResult{
            .block = std::move(call),
            .result_json = Json{{"error", kAskUserPipelineError}}.dump(),
            .is_error = true,
        };
      }
      auto resolution =
          tool_approval_->WaitForResolution(prepared.approval_id, stop_token);
      if (!resolution.approved) {
        constexpr auto kCancelledByUser = "Cancelled by user";
        call.is_error = true;
        call.error = kCancelledByUser;
        return ToolExecutionResult{
            .block = std::move(call),
            .result_json = Json{{"error", kCancelledByUser}}.dump(),
            .is_error = true,
        };
      }
      call.response = std::move(resolution.response);
      return ToolExecutionResult{
          .block = call,
          .result_json = Json{{"result", call.response}}.dump(),
      };
    }
    if (prepared.request.name == kFileEditToolName) {
      return ExecuteEditTool(prepared.request, workspace_filesystem_);
    }
    if (prepared.request.name == kGrepToolName) {
      return ExecuteGrepTool(prepared.request, workspace_filesystem_,
                             stop_token);
    }
    if (prepared.request.name == kGlobToolName) {
      return ExecuteGlobTool(prepared.request, workspace_filesystem_);
    }
    return ErrorResult(prepared.preview,
                       "Unknown tool: " + prepared.request.name);
  } catch (const std::exception& error) {
    return ErrorResult(prepared.preview, error.what());
  }
}

}  // namespace yac::tool_call
