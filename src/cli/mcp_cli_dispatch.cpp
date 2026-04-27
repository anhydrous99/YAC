#include "cli/mcp_cli_dispatch.hpp"

#include "mcp/mcp_server_config.hpp"
#include "mcp/oauth/flow.hpp"

#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace yac::cli {

namespace {

constexpr std::string_view kNoBrowserFlag = "--no-browser";
constexpr std::string_view kCallbackUrlPrefix = "--callback-url=";
constexpr std::string_view kUsage =
    "Usage: yac mcp <subcommand> [args]\n"
    "\n"
    "Subcommands:\n"
    "  add                    Add an MCP server (interactive wizard)\n"
    "  list                   List configured MCP servers\n"
    "  auth <id>              OAuth PKCE flow (browser + loopback)\n"
    "  auth <id> --no-browser\n"
    "                         Print URL; read callback URL from stdin\n"
    "  auth <id> --no-browser --callback-url=<URL>\n"
    "                         Non-interactive OAuth\n"
    "  logout <id>            Remove stored OAuth tokens\n"
    "  debug <id>             Print server status, auth, and log tail\n"
    "\n"
    "Exit codes: 0 success, 1 user error (bad args), 2 system error\n";

int DoList(McpAdminCommand& cmd, std::ostream& out) {
  auto servers = cmd.ListServers();
  if (servers.empty()) {
    out << "(no MCP servers configured)\n";
    return 0;
  }
  for (const auto& s : servers) {
    out << s.id << "  [" << s.state << "]  " << s.transport << "\n";
  }
  return 0;
}

int DoAdd(McpAdminCommand& cmd, std::ostream& out, std::ostream& err) {
  mcp::McpServerConfig cfg;

  out << "Server id: ";
  out.flush();
  if (!std::getline(std::cin, cfg.id) || cfg.id.empty()) {
    err << "Error: server id must not be empty\n";
    return 1;
  }

  out << "Transport (stdio / http): ";
  out.flush();
  if (!std::getline(std::cin, cfg.transport)) {
    err << "Error: transport required\n";
    return 1;
  }
  if (cfg.transport != "stdio" && cfg.transport != "http") {
    err << "Error: transport must be 'stdio' or 'http'\n";
    return 1;
  }

  if (cfg.transport == "stdio") {
    out << "Command (e.g. npx -y my-mcp): ";
    out.flush();
    if (!std::getline(std::cin, cfg.command) || cfg.command.empty()) {
      err << "Error: command required for stdio transport\n";
      return 1;
    }
  } else {
    out << "URL (e.g. https://example.com/mcp): ";
    out.flush();
    if (!std::getline(std::cin, cfg.url) || cfg.url.empty()) {
      err << "Error: url required for http transport\n";
      return 1;
    }
  }

  try {
    cmd.AddServer(std::move(cfg));
    out << "Server added.\n";
    return 0;
  } catch (const std::exception& e) {
    err << "Error: " << e.what() << "\n";
    return 2;
  }
}

int DoAuth(McpAdminCommand& cmd, std::string_view server_id, int extra_argc,
           char** extra_argv, std::ostream& out, std::ostream& err) {
  bool no_browser = false;
  std::optional<std::string> callback_url;

  for (int i = 0; i < extra_argc; ++i) {
    const std::string_view arg(extra_argv[i]);
    if (arg == kNoBrowserFlag) {
      no_browser = true;
    } else if (arg.starts_with(kCallbackUrlPrefix)) {
      callback_url = std::string(arg.substr(kCallbackUrlPrefix.size()));
    } else {
      err << "Error: unknown flag: " << arg << "\n";
      return 1;
    }
  }

  const mcp::oauth::OAuthInteractionMode mode{
      .browser_disabled = no_browser,
      .injected_callback_url = std::move(callback_url),
  };

  try {
    cmd.Authenticate(server_id, mode);
    out << "Authenticated successfully.\n";
    return 0;
  } catch (const std::exception& e) {
    err << "Error: " << e.what() << "\n";
    return 2;
  }
}

int DoLogout(McpAdminCommand& cmd, std::string_view server_id,
             std::ostream& out) {
  cmd.Logout(server_id);
  out << "Logged out: " << server_id << "\n";
  return 0;
}

int DoDebug(McpAdminCommand& cmd, std::string_view server_id, std::ostream& out,
            std::ostream& err) {
  try {
    const McpDebugReport report = cmd.Debug(server_id);
    out << "=== Status ===\n" << report.status << "\n";
    out << "=== Auth ===\n" << report.auth << "\n";
    out << "=== Connectivity ===\n" << report.connectivity << "\n";
    out << "=== Log (last 50 lines) ===\n" << report.log;
    return 0;
  } catch (const std::exception& e) {
    err << "Error: " << e.what() << "\n";
    return 2;
  }
}

}  // namespace

int RunMcpCli(int argc, char** argv, McpCliOptions opts) {
  std::ostream& out = opts.out != nullptr ? *opts.out : std::cout;
  std::ostream& err = opts.err != nullptr ? *opts.err : std::cerr;

  if (argc == 0) {
    out << kUsage;
    return 0;
  }

  const std::string_view subcmd(argv[0]);

  McpAdminCommand::Options admin_opts{
      .token_store = std::move(opts.token_store),
      .connectivity_test = std::move(opts.connectivity_test),
      .settings_path = std::move(opts.settings_path),
  };
  McpAdminCommand cmd(std::move(admin_opts));

  if (subcmd == "list") {
    if (argc > 1) {
      err << "Error: 'list' takes no arguments\n";
      return 1;
    }
    try {
      return DoList(cmd, out);
    } catch (const std::exception& e) {
      err << "Error: " << e.what() << "\n";
      return 2;
    }
  }

  if (subcmd == "add") {
    if (argc > 1) {
      err << "Error: 'add' takes no arguments\n";
      return 1;
    }
    return DoAdd(cmd, out, err);
  }

  if (subcmd == "auth") {
    if (argc < 2) {
      err << "Error: 'auth' requires a server id\n";
      return 1;
    }
    return DoAuth(cmd, argv[1], argc - 2, argv + 2, out, err);
  }

  if (subcmd == "logout") {
    if (argc != 2) {
      err << "Error: usage: yac mcp logout <id>\n";
      return 1;
    }
    return DoLogout(cmd, argv[1], out);
  }

  if (subcmd == "debug") {
    if (argc != 2) {
      err << "Error: usage: yac mcp debug <id>\n";
      return 1;
    }
    return DoDebug(cmd, argv[1], out, err);
  }

  err << "Error: unknown subcommand: " << subcmd
      << "\n  Run 'yac mcp' for usage.\n";
  return 1;
}

}  // namespace yac::cli
