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
#include "tool_call/tool_validation_error.hpp"

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
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteFileReadTool(prepared.request, workspace_filesystem);
}

ToolExecutionResult ExecuteFileWriteDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteFileWriteTool(prepared.request, workspace_filesystem);
}

ToolExecutionResult ExecuteListDirDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteListDirTool(prepared.request, workspace_filesystem);
}

ToolExecutionResult ExecuteLspDiagnosticsDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteLspDiagnosticsTool(prepared.request, *lsp_client,
                                   workspace_filesystem);
}

ToolExecutionResult ExecuteLspReferencesDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteLspReferencesTool(prepared.request, *lsp_client,
                                  workspace_filesystem);
}

ToolExecutionResult ExecuteLspGotoDefinitionDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteLspGotoDefinitionTool(prepared.request, *lsp_client,
                                      workspace_filesystem);
}

ToolExecutionResult ExecuteLspRenameDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteLspRenameTool(prepared.request, *lsp_client,
                              workspace_filesystem);
}

ToolExecutionResult ExecuteLspSymbolsDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& lsp_client, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteLspSymbolsTool(prepared.request, *lsp_client,
                               workspace_filesystem);
}

ToolExecutionResult ExecuteSubAgentDispatch(
    const PreparedToolCall& prepared, std::stop_token stop_token,
    const WorkspaceFilesystem& /*workspace*/,
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& /*todos*/,
    chat::SubAgentManager* sub_agent_manager,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteSubAgentTool(prepared, sub_agent_manager, stop_token);
}

ToolExecutionResult ExecuteTodoWriteDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& /*workspace*/,
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& todo_state,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  const auto* call = std::get_if<TodoWriteCall>(&prepared.preview);
  if (call == nullptr) {
    return ErrorResult(prepared.preview,
                       "Internal error: todo_write preview type mismatch.");
  }
  todo_state.Update(call->todos);
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
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteBashTool(prepared.request, workspace_filesystem.Root(),
                         stop_token);
}

ToolExecutionResult ExecuteAskUserDispatch(
    const PreparedToolCall& prepared, std::stop_token stop_token,
    const WorkspaceFilesystem& /*workspace*/,
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* tool_approval) {
  const auto* call_ptr = std::get_if<AskUserCall>(&prepared.preview);
  if (call_ptr == nullptr) {
    return ErrorResult(prepared.preview,
                       "Internal error: ask_user preview type mismatch.");
  }
  auto call = *call_ptr;
  if (tool_approval == nullptr || prepared.approval_id.empty()) {
    return ErrorResult(std::move(call),
                       "Ask user approval pipeline unavailable.");
  }
  auto resolution =
      tool_approval->WaitForResolution(prepared.approval_id, stop_token);
  if (!resolution.approved) {
    return ErrorResult(std::move(call), "Cancelled by user");
  }
  call.response = std::move(resolution.response);
  return ToolExecutionResult{
      .block = call,
      .result_json = Json{{"result", call.response}}.dump(),
  };
}

ToolExecutionResult ExecuteFileEditDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteEditTool(prepared.request, workspace_filesystem);
}

ToolExecutionResult ExecuteGrepDispatch(
    const PreparedToolCall& prepared, std::stop_token stop_token,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteGrepTool(prepared.request, workspace_filesystem, stop_token);
}

ToolExecutionResult ExecuteGlobDispatch(
    const PreparedToolCall& prepared, std::stop_token /*stop*/,
    const WorkspaceFilesystem& workspace_filesystem,
    const std::shared_ptr<ILspClient>& /*lsp*/, TodoState& /*todos*/,
    chat::SubAgentManager* /*agents*/,
    chat::internal::ChatServiceToolApproval* /*approval*/) {
  return ExecuteGlobTool(prepared.request, workspace_filesystem);
}

const std::unordered_map<std::string_view, ExecuteFn> kExecuteRegistry = {
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
  return kExecuteRegistry.contains(name);
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
      const auto definitions = ToolDefinitions();
      ToolValidationError validation("Unknown tool: " + prepared.request.name,
                                     prepared.request.name,
                                     prepared.request.arguments_json);
      return ErrorResult(
          prepared.preview, validation.what(),
          Json::parse(BuildValidationErrorJson(validation, definitions)));
    }
    return it->second(prepared, stop_token, workspace_filesystem_, lsp_client_,
                      todo_state_, sub_agent_manager_, tool_approval_);
  } catch (const ToolValidationError& error) {
    const auto definitions = ToolDefinitions();
    return ErrorResult(
        prepared.preview, error.what(),
        Json::parse(BuildValidationErrorJson(error, definitions)));
  } catch (const std::exception& error) {
    return ErrorResult(prepared.preview, error.what());
  }
}

}  // namespace yac::tool_call
