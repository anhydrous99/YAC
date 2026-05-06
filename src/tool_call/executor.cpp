#include "tool_call/executor.hpp"

#include "tool_call/executor_catalog.hpp"
#include "tool_call/tool_error_result.hpp"
#include "tool_call/tool_validation_error.hpp"

#include <utility>

namespace yac::tool_call {

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
  const ExecutionContext ctx{
      .workspace_filesystem = workspace_filesystem_,
      .lsp_client = lsp_client_,
      .todo_state = todo_state_,
      .sub_agent_manager = sub_agent_manager_,
      .tool_approval = tool_approval_,
      .stop = stop_token,
  };
  try {
    const auto* handler = LookupToolHandler(prepared.request.name);
    if (handler == nullptr) {
      const auto definitions = ToolDefinitions();
      ToolValidationError validation("Unknown tool: " + prepared.request.name,
                                     prepared.request.name,
                                     prepared.request.arguments_json);
      return ErrorResult(
          prepared.preview, validation.what(),
          Json::parse(BuildValidationErrorJson(validation, definitions)));
    }
    return handler->execute(prepared, ctx);
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
