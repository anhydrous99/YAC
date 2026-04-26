#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace yac::mcp {

struct McpAuthBearer {
  std::string api_key_env;
};

struct McpAuthOAuth {
  std::string authorization_url;
  std::string token_url;
  std::string client_id;
  std::vector<std::string> scopes;
};

struct McpServerConfig {
  std::string id;
  std::string transport;  // "stdio" or "http"
  std::string command;
  std::vector<std::string> args;
  std::unordered_map<std::string, std::string> env;
  std::string url;
  bool enabled = true;
  bool requires_approval = false;
  std::vector<std::string> approval_required_tools;
  bool auto_start = true;
  std::optional<std::variant<McpAuthBearer, McpAuthOAuth>> auth;
  std::unordered_map<std::string, std::string> headers;
};

struct McpConfig {
  std::vector<McpServerConfig> servers;
  uintmax_t result_max_bytes = 256 * 1024;
};

const std::unordered_map<std::string, McpServerConfig>& McpServerPresets();

}  // namespace yac::mcp
