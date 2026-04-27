#pragma once

#include "core_types/mcp_manager_interface.hpp"
#include "core_types/mcp_resource_types.hpp"
#include "core_types/mcp_tool_catalog_snapshot.hpp"
#include "core_types/tool_call_types.hpp"

#include <stop_token>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace yac::test {

class MockMcpManager : public core_types::IMcpManager {
 public:
  void AddTool(std::string server_id, std::string tool_name,
               bool requires_approval = false) {
    using namespace tool_call;
    std::string qualified = std::string{kMcpToolNamePrefix} + server_id +
                            std::string{kMcpToolNameSeparator} + tool_name;
    snapshot_.tools.push_back(
        chat::ToolDefinition{.name = qualified,
                             .description = "Mock " + tool_name,
                             .parameters_schema_json = "{}"});
    snapshot_.name_to_server_tool[qualified] = {server_id, tool_name};
    snapshot_.approval_policy[qualified] =
        yac::core_types::McpToolApprovalPolicy{.requires_approval =
                                                   requires_approval};
  }

  void SetInvokeResult(std::string result_json) {
    invoke_result_json_ = std::move(result_json);
  }

  [[nodiscard]] core_types::McpToolCatalogSnapshot GetToolCatalogSnapshot()
      const override {
    return snapshot_;
  }

  core_types::ToolExecutionResult InvokeTool(std::string_view qualified_name,
                                             std::string_view arguments_json,
                                             std::stop_token) override {
    ++invoke_count;
    const auto it =
        snapshot_.name_to_server_tool.find(std::string{qualified_name});
    tool_call::McpToolCall call{
        .server_id = it != snapshot_.name_to_server_tool.end()
                         ? it->second.first
                         : std::string{},
        .tool_name = std::string{qualified_name},
        .original_tool_name = it != snapshot_.name_to_server_tool.end()
                                  ? it->second.second
                                  : std::string{},
        .arguments_json = std::string{arguments_json},
        .result_blocks = {tool_call::McpResultBlock{
            .kind = tool_call::McpResultBlockKind::Text,
            .text = invoke_result_json_}}};
    return core_types::ToolExecutionResult{.block = std::move(call),
                                           .result_json = invoke_result_json_,
                                           .is_error = false};
  }

  [[nodiscard]] std::vector<core_types::McpServerStatus>
  GetServerStatusSnapshot() const override {
    return {};
  }

  std::vector<core_types::McpResourceDescriptor> ListResources(
      std::string_view, std::stop_token) override {
    return {};
  }

  core_types::McpResourceContent ReadResource(std::string_view,
                                              std::string_view,
                                              std::stop_token) override {
    return {};
  }

  int invoke_count = 0;

 private:
  core_types::McpToolCatalogSnapshot snapshot_;
  std::string invoke_result_json_ = R"({"result":"ok"})";
};

}  // namespace yac::test
