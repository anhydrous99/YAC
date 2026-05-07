#pragma once

#include "core_types/mcp_resource_types.hpp"
#include "core_types/typed_ids.hpp"
#include "mcp/mcp_server_config.hpp"
#include "mcp/oauth/flow.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace yac::mcp {
class ITokenStore;
}  // namespace yac::mcp

namespace yac::cli {

// Output of McpAdminCommand::Debug. Each field is a rendered section.
struct McpDebugReport {
  ::yac::McpServerId server_id;
  // Status: state, transport, last error.
  std::string status;
  // Auth: whether a token is present and its expiry (no secret values).
  std::string auth;
  // Connectivity: PASS or FAIL with detail.
  std::string connectivity;
  // Recent log: last 50 lines of the server debug log, secrets redacted.
  std::string log;
};

// Shared core handler for MCP administration. Both CLI (T40) and TUI (T41)
// adapters delegate to this class — do not duplicate logic there.
class McpAdminCommand {
 public:
  // Optional function that probes a server for connectivity; return true on
  // success. When null the command falls back to a best-effort HTTP/stdio
  // probe.
  using ConnectivityTestFn = std::function<bool(const mcp::McpServerConfig&)>;

  struct Options {
    // Token store used for OAuth tokens. When null, McpAdminCommand
    // auto-detects: keychain when available, file store as fallback.
    std::shared_ptr<mcp::ITokenStore> token_store;

    // Connectivity probe override. null = built-in default probe.
    ConnectivityTestFn connectivity_test;

    // Path to settings.toml. Empty = ~/.yac/settings.toml.
    std::filesystem::path settings_path;
  };

  explicit McpAdminCommand(Options opts = {});

  // Validates config then appends an [[mcp.servers]] block to settings.toml,
  // preserving all comments. Throws std::runtime_error on validation
  // failure or I/O error. Throws if a server with the same id already exists.
  void AddServer(mcp::McpServerConfig config);

  // Reads settings.toml and returns one McpServerStatus per configured
  // server. State is "configured" (the command has no live manager).
  [[nodiscard]] std::vector<core_types::McpServerStatus> ListServers();

  // Runs the OAuth PKCE flow for the named server and persists the resulting
  // tokens via the token store. Throws if the server is not configured for
  // OAuth or if the flow is cancelled.
  void Authenticate(std::string_view server_id,
                    const mcp::oauth::OAuthInteractionMode& mode);

  // Clears the stored OAuth tokens for the named server (silently ignored if
  // no token exists).
  void Logout(std::string_view server_id);

  // Builds a McpDebugReport for the named server: status section, auth
  // section (token present / expiry — no raw secrets), connectivity probe
  // (PASS/FAIL), and the last 50 lines of the debug log (redacted).
  // Throws std::runtime_error if the server id is not found in config.
  [[nodiscard]] McpDebugReport Debug(std::string_view server_id);

 private:
  [[nodiscard]] std::filesystem::path ResolveSettingsPath() const;
  [[nodiscard]] mcp::ITokenStore& GetTokenStore();
  [[nodiscard]] mcp::McpConfig LoadMcpConfig() const;
  [[nodiscard]] static const mcp::McpServerConfig& FindServer(
      const mcp::McpConfig& config, std::string_view server_id);

  Options opts_;
  mutable std::shared_ptr<mcp::ITokenStore> token_store_cache_;
};

}  // namespace yac::cli
