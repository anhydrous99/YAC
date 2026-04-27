#pragma once

#include "chat/types.hpp"
#include "core_types/mcp_tool_catalog_snapshot.hpp"
#include "tool_call/executor.hpp"

#include <functional>
#include <stop_token>
#include <vector>

namespace yac::core_types {
class IMcpManager;
}  // namespace yac::core_types

namespace yac::chat::internal {

class ChatServiceMcp {
 public:
  using EmitEventFn = std::function<void(ChatEvent)>;

  explicit ChatServiceMcp(core_types::IMcpManager* manager);

  [[nodiscard]] core_types::McpToolCatalogSnapshot BuildToolCatalogSnapshot();

  [[nodiscard]] static std::vector<ToolDefinition> MergeBuiltInsAndMcp(
      const std::vector<ToolDefinition>& built_ins,
      const core_types::McpToolCatalogSnapshot& snapshot);

  [[nodiscard]] tool_call::PreparedToolCall PrepareMcpToolCall(
      const ToolCallRequest& request);

  [[nodiscard]] tool_call::ToolExecutionResult ExecuteMcpToolCall(
      const tool_call::PreparedToolCall& prepared, std::stop_token stop);

  void RegisterEventHandlers(EmitEventFn parent_emit);

 private:
  core_types::IMcpManager* manager_;
  EmitEventFn emit_event_;
};

}  // namespace yac::chat::internal
