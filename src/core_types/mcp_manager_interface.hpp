#pragma once

#include "core_types/mcp_resource_types.hpp"
#include "core_types/mcp_tool_catalog_snapshot.hpp"
#include "core_types/tool_call_types.hpp"

#include <stop_token>
#include <string_view>
#include <vector>

namespace yac::core_types {

class IMcpManager {
 public:
  IMcpManager() = default;
  virtual ~IMcpManager() = default;
  IMcpManager(const IMcpManager&) = delete;
  IMcpManager& operator=(const IMcpManager&) = delete;
  IMcpManager(IMcpManager&&) = delete;
  IMcpManager& operator=(IMcpManager&&) = delete;

  [[nodiscard]]
  virtual McpToolCatalogSnapshot GetToolCatalogSnapshot() const = 0;
  virtual ToolExecutionResult InvokeTool(std::string_view qualified_name,
                                         std::string_view arguments_json,
                                         std::stop_token stop) = 0;
  [[nodiscard]]
  virtual std::vector<McpServerStatus> GetServerStatusSnapshot() const = 0;
  virtual std::vector<McpResourceDescriptor> ListResources(
      std::string_view server_id, std::stop_token stop) = 0;
  virtual McpResourceContent ReadResource(std::string_view server_id,
                                          std::string_view uri,
                                          std::stop_token stop) = 0;
};

}  // namespace yac::core_types
