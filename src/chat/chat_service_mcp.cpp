#include "chat/chat_service_mcp.hpp"

#include "core_types/mcp_manager_interface.hpp"
#include "core_types/mcp_tool_catalog_snapshot.hpp"
#include "core_types/tool_call_types.hpp"
#include "tool_call/executor_catalog.hpp"

#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace yac::chat::internal {

ChatServiceMcp::ChatServiceMcp(core_types::IMcpManager* manager)
    : manager_(manager) {}

core_types::McpToolCatalogSnapshot ChatServiceMcp::BuildToolCatalogSnapshot() {
  return manager_->GetToolCatalogSnapshot();
}

std::vector<ToolDefinition> ChatServiceMcp::MergeBuiltInsAndMcp(
    const std::vector<ToolDefinition>& built_ins,
    const core_types::McpToolCatalogSnapshot& snapshot) {
  std::unordered_set<std::string> names;
  names.reserve(built_ins.size());
  for (const auto& def : built_ins) {
    names.insert(def.name);
  }
  for (const auto& def : snapshot.tools) {
    if (!names.insert(def.name).second) {
      throw std::invalid_argument("MCP tool name collision: " + def.name);
    }
  }

  std::vector<ToolDefinition> merged;
  merged.reserve(built_ins.size() + snapshot.tools.size());
  merged.insert(merged.end(), built_ins.begin(), built_ins.end());
  merged.insert(merged.end(), snapshot.tools.begin(), snapshot.tools.end());

  // Validate all tool names against Bedrock compliance regex
  tool_call::ValidateToolNames(merged);

  return merged;
}

tool_call::PreparedToolCall ChatServiceMcp::PrepareMcpToolCall(
    const ToolCallRequest& request) {
  const auto snapshot = manager_->GetToolCatalogSnapshot();

  const auto it = snapshot.name_to_server_tool.find(request.name);
  if (it == snapshot.name_to_server_tool.end()) {
    throw std::invalid_argument("Unknown MCP tool: " + request.name);
  }

  const auto& [server_id, original_tool_name] = it->second;

  const auto policy_it = snapshot.approval_policy.find(request.name);
  const bool requires_approval = policy_it != snapshot.approval_policy.end() &&
                                 policy_it->second.requires_approval;

  tool_call::McpToolCall block{.server_id = server_id,
                               .tool_name = request.name,
                               .original_tool_name = original_tool_name,
                               .arguments_json = request.arguments_json};

  if (policy_it != snapshot.approval_policy.end()) {
    block.server_requires_approval = policy_it->second.server_requires_approval;
    block.approval_required_tools = policy_it->second.approval_required_tools;
  }

  return tool_call::PreparedToolCall{
      .request = request,
      .preview = std::move(block),
      .requires_approval = requires_approval,
      .approval_prompt =
          "Call MCP tool " + server_id + "/" + original_tool_name};
}

tool_call::ToolExecutionResult ChatServiceMcp::ExecuteMcpToolCall(
    const tool_call::PreparedToolCall& prepared, std::stop_token stop) {
  return manager_->InvokeTool(prepared.request.name,
                              prepared.request.arguments_json, stop);
}

void ChatServiceMcp::RegisterEventHandlers(EmitEventFn parent_emit) {
  emit_event_ = std::move(parent_emit);
}

}  // namespace yac::chat::internal
