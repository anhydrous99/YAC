#include "mcp/mcp_server_config.hpp"

namespace yac::mcp {

const std::unordered_map<std::string, McpServerConfig>& McpServerPresets() {
  static const std::unordered_map<std::string, McpServerConfig> presets = {
      {"context7",
       {.id = "context7",
        .transport = "stdio",
        .command = "npx",
        .args = {"-y", "@upstash/context7-mcp"}}},
  };
  return presets;
}

}  // namespace yac::mcp
