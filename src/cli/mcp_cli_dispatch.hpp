#pragma once

#include "cli/mcp_admin_command.hpp"

#include <filesystem>
#include <memory>
#include <ostream>

namespace yac::cli {

struct McpCliOptions {
  std::filesystem::path settings_path;  // empty → ~/.yac/settings.toml
  std::shared_ptr<mcp::ITokenStore> token_store;  // null → auto-detect
  McpAdminCommand::ConnectivityTestFn connectivity_test;  // null → default
  std::ostream* out = nullptr;                            // null → std::cout
  std::ostream* err = nullptr;                            // null → std::cerr
};

// argv[0] is the subcommand; argc==0 prints usage.
// Returns 0 success, 1 user error (bad args), 2 system error.
[[nodiscard]] int RunMcpCli(int argc, char** argv, McpCliOptions opts = {});

}  // namespace yac::cli
