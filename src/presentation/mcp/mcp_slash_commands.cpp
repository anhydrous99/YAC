#include "presentation/mcp/mcp_slash_commands.hpp"

namespace yac::presentation {

void RegisterMcpSlashCommands(SlashCommandRegistry& registry) {
  registry.Define("mcp", "mcp",
                  "Manage MCP servers (add|list|auth|logout|debug|resources)");
}

std::vector<Command> BuildMcpPaletteCommands() {
  return {
      Command{"mcp_list", "MCP: List Servers", "List configured MCP servers"},
      Command{"mcp_add", "MCP: Add Server", "Add a new MCP server to settings"},
  };
}

}  // namespace yac::presentation
