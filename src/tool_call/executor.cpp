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
#include "tool_call/tool_error_result.hpp"

#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace yac::tool_call {

namespace {

using ExecuteFn = ToolExecutionResult (*)(
    const PreparedToolCall& prepared, std::stop_token stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState& todo_state,
    chat::SubAgentManager* sub_agent_manager,
    chat::internal::ChatServiceToolApproval* tool_approval);

ToolExecutionResult ExecuteFileReadDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>&, TodoState&, chat::SubAgentManager*,
    chat::internal::ChatServiceToolApproval*) {
  return ExecuteFileReadTool(prepared.request, workspace_filesystem);
}

ToolExecutionResult ExecuteFileWriteDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>&, TodoState&, chat::SubAgentManager*,
    chat::internal::ChatServiceToolApproval*) {
  return ExecuteFileWriteTool(prepared.request, workspace_filesystem);
}

ToolExecutionResult ExecuteListDirDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>&, TodoState&, chat::SubAgentManager*,
    chat::internal::ChatServiceToolApproval*) {
  return ExecuteListDirTool(prepared.request, workspace_filesystem);
}

ToolExecutionResult ExecuteLspDiagnosticsDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState&,
    chat::SubAgentManager*, chat::internal::ChatServiceToolApproval*) {
  return ExecuteLspDiagnosticsTool(prepared.request, *lsp_client,
                                   workspace_filesystem);
}

ToolExecutionResult ExecuteLspReferencesDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState&,
    chat::SubAgentManager*, chat::internal::ChatServiceToolApproval*) {
  return ExecuteLspReferencesTool(prepared.request, *lsp_client,
                                  workspace_filesystem);
}

ToolExecutionResult ExecuteLspGotoDefinitionDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState&,
    chat::SubAgentManager*, chat::internal::ChatServiceToolApproval*) {
  return ExecuteLspGotoDefinitionTool(prepared.request, *lsp_client,
                                      workspace_filesystem);
}

ToolExecutionResult ExecuteLspRenameDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState&,
    chat::SubAgentManager*, chat::internal::ChatServiceToolApproval*) {
  return ExecuteLspRenameTool(prepared.request, *lsp_client,
                              workspace_filesystem);
}

ToolExecutionResult ExecuteLspSymbolsDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState&,
    chat::SubAgentManager*, chat::internal::ChatServiceToolApproval*) {
  return ExecuteLspSymbolsTool(prepared.request, *lsp_client,
                               workspace_filesystem);
}

ToolExecutionResult ExecuteSubAgentDispatch(
    const PreparedToolCall& prepared, std::stop_token stop_token,
    const WorkspaceFilesystem&, const std::shared_ptr<ILspClient>&, TodoState&,
    chat::SubAgentManager* sub_agent_manager,
    chat::internal::ChatServiceToolApproval*) {
  return ExecuteSubAgentTool(prepared, sub_agent_manager, stop_token);
}

ToolExecutionResult ExecuteTodoWriteDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem&, const std::shared_ptr<ILspClient>&,
    TodoState& todo_state, chat::SubAgentManager*,
    chat::internal::ChatServiceToolApproval*) {
  const auto& call = std::get<TodoWriteCall>(prepared.preview);
  todo_state.Update(call.todos);
  auto current = todo_state.Current();
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

ToolExecutionResult ExecuteBashDispatch(
    const PreparedToolCall& prepared, std::stop_token stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>&, TodoState&, chat::SubAgentManager*,
    chat::internal::ChatServiceToolApproval*) {
  return ExecuteBashTool(prepared.request, workspace_filesystem.Root(),
                         stop_token);
}

ToolExecutionResult ExecuteAskUserDispatch(
    const PreparedToolCall& prepared, std::stop_token stop_token,
    const WorkspaceFilesystem&, const std::shared_ptr<ILspClient>&, TodoState&,
    chat::SubAgentManager*,
    chat::internal::ChatServiceToolApproval* tool_approval) {
  auto call = std::get<AskUserCall>(prepared.preview);
  if (tool_approval == nullptr || prepared.approval_id.empty()) {
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
      tool_approval->WaitForResolution(prepared.approval_id, stop_token);
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

ToolExecutionResult ExecuteFileEditDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>&, TodoState&, chat::SubAgentManager*,
    chat::internal::ChatServiceToolApproval*) {
  return ExecuteEditTool(prepared.request, workspace_filesystem);
}

ToolExecutionResult ExecuteGrepDispatch(
    const PreparedToolCall& prepared, std::stop_token stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>&, TodoState&, chat::SubAgentManager*,
    chat::internal::ChatServiceToolApproval*) {
  return ExecuteGrepTool(prepared.request, workspace_filesystem, stop_token);
}

ToolExecutionResult ExecuteGlobDispatch(
    const PreparedToolCall& prepared, std::stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>&, TodoState&, chat::SubAgentManager*,
    chat::internal::ChatServiceToolApproval*) {
  return ExecuteGlobTool(prepared.request, workspace_filesystem);
}

static const std::unordered_map<std::string_view, ExecuteFn> kExecuteRegistry =
    {
        {kFileReadToolName, &ExecuteFileReadDispatch},
        {kFileWriteToolName, &ExecuteFileWriteDispatch},
        {kListDirToolName, &ExecuteListDirDispatch},
        {kLspDiagnosticsToolName, &ExecuteLspDiagnosticsDispatch},
        {kLspReferencesToolName, &ExecuteLspReferencesDispatch},
        {kLspGotoDefinitionToolName, &ExecuteLspGotoDefinitionDispatch},
        {kLspRenameToolName, &ExecuteLspRenameDispatch},
        {kLspSymbolsToolName, &ExecuteLspSymbolsDispatch},
        {kSubAgentToolName, &ExecuteSubAgentDispatch},
        {kTodoWriteToolName, &ExecuteTodoWriteDispatch},
        {kBashToolName, &ExecuteBashDispatch},
        {kAskUserToolName, &ExecuteAskUserDispatch},
        {kFileEditToolName, &ExecuteFileEditDispatch},
        {kGrepToolName, &ExecuteGrepDispatch},
        {kGlobToolName, &ExecuteGlobDispatch},
};

}  // namespace

bool HasToolExecutorDispatchEntry(std::string_view name) {
  return kExecuteRegistry.find(name) != kExecuteRegistry.end();
}

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
    const auto it = kExecuteRegistry.find(prepared.request.name);
    if (it == kExecuteRegistry.end()) {
      return ErrorResult(prepared.preview,
                         "Unknown tool: " + prepared.request.name);
    }
    return it->second(prepared, stop_token, workspace_filesystem_, lsp_client_,
                      todo_state_, sub_agent_manager_, tool_approval_);
  } catch (const std::exception& error) {
    return ErrorResult(prepared.preview, error.what());
  }
}

}  // namespace yac::tool_call
