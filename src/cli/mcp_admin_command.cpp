#include "cli/mcp_admin_command.hpp"

#include "chat/config_paths.hpp"
#include "chat/settings_toml.hpp"
#include "chat/types.hpp"
#include "mcp/file_token_store.hpp"
#include "mcp/keychain_token_store.hpp"
#include "mcp/oauth/loopback_callback.hpp"
#include "mcp/oauth/pkce.hpp"
#include "mcp/secret_redaction.hpp"
#include "mcp/token_store.hpp"
#include "util/log.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

namespace yac::cli {

namespace {

using Json = nlohmann::json;

constexpr int kLogTailLines = 50;

std::string ReadFileText(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return {};
  }
  std::ostringstream buf;
  buf << input.rdbuf();
  return buf.str();
}

void WriteFileText(const std::filesystem::path& path,
                   std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    throw std::runtime_error("Cannot open for writing: " + path.string());
  }
  output << content;
  output.close();
  if (!output) {
    throw std::runtime_error("I/O error writing: " + path.string());
  }
}

bool ServerIdExistsInToml(std::string_view toml_text, std::string_view id) {
  try {
    auto table = toml::parse(toml_text);
    const auto mcp_node = table["mcp"];
    if (!mcp_node.is_table()) {
      return false;
    }
    const auto servers_node = (*mcp_node.as_table())["servers"];
    if (!servers_node.is_array()) {
      return false;
    }
    for (const auto& elem : *servers_node.as_array()) {
      if (const auto* tbl = elem.as_table()) {
        if (const auto* v = (*tbl)["id"].as_string()) {
          if (v->get() == id) {
            return true;
          }
        }
      }
    }
  } catch (...) {
    // SAFETY: filesystem probing is best-effort; treat any failure as "not
    // found" so callers proceed with their default behavior.
    yac::log::Warn("cli.mcp_admin", "TOML probe for server id failed: {}",
                   yac::log::DescribeCurrentException());
  }
  return false;
}

std::string QuoteToml(std::string_view value) {
  std::ostringstream out;
  out << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        out << "\\\\";
        break;
      case '"':
        out << "\\\"";
        break;
      case '\n':
        out << "\\n";
        break;
      case '\r':
        out << "\\r";
        break;
      case '\t':
        out << "\\t";
        break;
      default:
        if (ch < 0x20) {
          out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
              << static_cast<int>(ch) << std::dec;
        } else {
          out << static_cast<char>(ch);
        }
    }
  }
  out << '"';
  return out.str();
}

std::string BuildMcpServerTomlBlock(const mcp::McpServerConfig& cfg) {
  std::ostringstream out;
  out << "\n[[mcp.servers]]\n";
  out << "id = " << QuoteToml(cfg.id) << "\n";
  out << "transport = " << QuoteToml(cfg.transport) << "\n";
  if (!cfg.command.empty()) {
    out << "command = " << QuoteToml(cfg.command) << "\n";
  }
  if (!cfg.args.empty()) {
    out << "args = [";
    for (std::size_t i = 0; i < cfg.args.size(); ++i) {
      if (i > 0) {
        out << ", ";
      }
      out << QuoteToml(cfg.args[i]);
    }
    out << "]\n";
  }
  if (!cfg.url.empty()) {
    out << "url = " << QuoteToml(cfg.url) << "\n";
  }
  if (!cfg.enabled) {
    out << "enabled = false\n";
  }
  if (cfg.requires_approval) {
    out << "requires_approval = true\n";
  }
  if (!cfg.env.empty()) {
    out << "[mcp.servers.env]\n";
    for (const auto& [k, v] : cfg.env) {
      out << QuoteToml(k) << " = " << QuoteToml(v) << "\n";
    }
  }
  return out.str();
}

void ValidateServerConfig(const mcp::McpServerConfig& cfg) {
  if (cfg.id.empty()) {
    throw std::runtime_error("MCP server id must not be empty");
  }
  if (cfg.transport != "stdio" && cfg.transport != "http") {
    throw std::runtime_error("MCP server transport must be 'stdio' or 'http'");
  }
  if (cfg.transport == "stdio" && cfg.command.empty()) {
    throw std::runtime_error("stdio transport requires a non-empty 'command'");
  }
  if (cfg.transport == "http" && cfg.url.empty()) {
    throw std::runtime_error("http transport requires a non-empty 'url'");
  }
}

std::string SanitizeServerId(std::string_view server_id) {
  std::string result;
  result.reserve(server_id.size());
  for (char c : server_id) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
        c == '_') {
      result += c;
    } else {
      result += '_';
    }
  }
  return result.empty() ? "server" : result;
}

std::filesystem::path DebugLogPath(std::string_view server_id) {
  const auto home = yac::chat::ResolveHomeDir();
  if (!home) {
    throw std::runtime_error("Cannot resolve home directory");
  }
  return yac::chat::GetYacConfigDir(*home) / "logs" / "mcp" /
         (SanitizeServerId(server_id) + ".log");
}

std::string TailLines(const std::filesystem::path& path, int n) {
  const std::string content = ReadFileText(path);
  if (content.empty()) {
    return {};
  }
  std::vector<std::string_view> lines;
  std::size_t pos = 0;
  while (pos <= content.size()) {
    const auto nl = content.find('\n', pos);
    const auto end = nl == std::string::npos ? content.size() : nl;
    if (end > pos || nl != std::string::npos) {
      lines.push_back(std::string_view(content).substr(pos, end - pos));
    }
    if (nl == std::string::npos) {
      break;
    }
    pos = nl + 1;
  }
  const auto start = lines.size() > static_cast<std::size_t>(n)
                         ? lines.size() - static_cast<std::size_t>(n)
                         : 0;
  std::string result;
  for (std::size_t i = start; i < lines.size(); ++i) {
    result += lines[i];
    result += '\n';
  }
  return result;
}

std::string FormatTokenExpiry(long long epoch_seconds) {
  const long long now_sec =
      std::chrono::duration_cast<std::chrono::seconds>(
          std::chrono::system_clock::now().time_since_epoch())
          .count();
  const long long diff_sec = epoch_seconds - now_sec;
  if (diff_sec <= 0) {
    return "expired";
  }
  const long long remaining_min = diff_sec / 60;
  if (remaining_min < 60) {
    return "expires in " + std::to_string(remaining_min) + "m";
  }
  const long long hours = remaining_min / 60;
  return "expires in " + std::to_string(hours) + "h " +
         std::to_string(remaining_min % 60) + "m";
}

bool DefaultConnectivityTest(const mcp::McpServerConfig& cfg) {
  if (cfg.transport == "http" && !cfg.url.empty()) {
    return !cfg.url.empty();
  }
  return !cfg.command.empty();
}

bool UseFileTokenStoreOverride() {
  const char* value = std::getenv("YAC_MCP_TOKEN_STORE");
  return value != nullptr && std::string_view(value) == "file";
}

}  // namespace

McpAdminCommand::McpAdminCommand(Options opts) : opts_(std::move(opts)) {}

std::filesystem::path McpAdminCommand::ResolveSettingsPath() const {
  if (!opts_.settings_path.empty()) {
    return opts_.settings_path;
  }
  return yac::chat::GetSettingsPath();
}

mcp::ITokenStore& McpAdminCommand::GetTokenStore() {
  if (!token_store_cache_) {
    if (opts_.token_store) {
      token_store_cache_ = opts_.token_store;
    } else if (!UseFileTokenStoreOverride() &&
               mcp::KeychainTokenStore::IsKeychainAvailable()) {
      token_store_cache_ = std::make_shared<mcp::KeychainTokenStore>();
    } else {
      token_store_cache_ = std::make_shared<mcp::FileTokenStore>();
    }
  }
  return *token_store_cache_;
}

mcp::McpConfig McpAdminCommand::LoadMcpConfig() const {
  const auto path = ResolveSettingsPath();
  yac::chat::ChatConfig cfg;
  std::vector<yac::chat::ConfigIssue> issues;
  yac::chat::LoadSettingsFromToml(path, cfg, issues);
  return cfg.mcp;
}

const mcp::McpServerConfig& McpAdminCommand::FindServer(
    const mcp::McpConfig& config, std::string_view server_id) {
  for (const auto& srv : config.servers) {
    if (srv.id == server_id) {
      return srv;
    }
  }
  throw std::runtime_error("MCP server not found: " + std::string(server_id));
}

void McpAdminCommand::AddServer(mcp::McpServerConfig config) {
  ValidateServerConfig(config);

  const auto path = ResolveSettingsPath();
  std::string content = ReadFileText(path);

  if (!content.empty() && ServerIdExistsInToml(content, config.id)) {
    throw std::runtime_error("MCP server already exists: " + config.id);
  }

  const std::string block = BuildMcpServerTomlBlock(config);

  std::string updated = content;
  if (!updated.empty() && updated.back() != '\n') {
    updated += '\n';
  }
  updated += block;

  try {
    static_cast<void>(toml::parse(updated));
  } catch (const toml::parse_error& e) {
    throw std::runtime_error(std::string("Generated invalid TOML: ") +
                             e.description().data());
  }

  WriteFileText(path, updated);
}

std::vector<core_types::McpServerStatus> McpAdminCommand::ListServers() {
  const auto mc = LoadMcpConfig();
  std::vector<core_types::McpServerStatus> result;
  result.reserve(mc.servers.size());
  for (const auto& srv : mc.servers) {
    core_types::McpServerStatus s;
    s.id = srv.id;
    s.state = "configured";
    s.transport = srv.transport;
    result.push_back(std::move(s));
  }
  return result;
}

void McpAdminCommand::Authenticate(
    std::string_view server_id, const mcp::oauth::OAuthInteractionMode& mode) {
  const auto mc = LoadMcpConfig();
  const auto& srv = FindServer(mc, server_id);

  if (!srv.auth.has_value() ||
      !std::holds_alternative<mcp::McpAuthOAuth>(*srv.auth)) {
    throw std::runtime_error("Server '" + std::string(server_id) +
                             "' is not configured for OAuth");
  }

  const auto& oauth_auth = std::get<mcp::McpAuthOAuth>(*srv.auth);
  const std::string verifier = mcp::oauth::GenerateCodeVerifier();
  const std::string challenge = mcp::oauth::DeriveCodeChallenge(verifier);
  const std::string state = mcp::oauth::GenerateCodeVerifier();

  mcp::oauth::LoopbackCallbackServer cb_server;
  mcp::oauth::OAuthFlow flow;

  const mcp::oauth::OAuthConfig oauth_config{
      .authorization_url = oauth_auth.authorization_url,
      .token_url = oauth_auth.token_url,
      .client_id = oauth_auth.client_id,
      .scopes = oauth_auth.scopes,
      .resource_url = srv.url,
  };

  const std::string redirect_uri = cb_server.RedirectUri();
  const std::string auth_url = flow.BuildAuthorizationUrl(
      oauth_config, challenge, state, redirect_uri, srv.url);

  std::stop_source stop_src;
  const auto callback =
      mcp::oauth::RunOAuthInteraction(mode, auth_url, stop_src.get_token());
  if (!callback.has_value()) {
    throw std::runtime_error("OAuth authentication cancelled");
  }

  flow.ValidateState(callback->second);
  const auto tokens = flow.ExchangeCode(oauth_config, callback->first, verifier,
                                        redirect_uri, srv.url);

  const auto expires_at = std::chrono::duration_cast<std::chrono::seconds>(
                              tokens.expires_at.time_since_epoch())
                              .count();
  const std::string token_json = Json{
      {"access_token", tokens.access_token},
      {"refresh_token", tokens.refresh_token},
      {"expires_at", expires_at},
      {"token_type", tokens.token_type},
      {"scope", tokens.scope}}.dump();

  GetTokenStore().Set(server_id, token_json);
}

void McpAdminCommand::Logout(std::string_view server_id) {
  GetTokenStore().Erase(server_id);
}

McpDebugReport McpAdminCommand::Debug(std::string_view server_id) {
  const auto mc = LoadMcpConfig();
  const auto& srv = FindServer(mc, server_id);

  McpDebugReport report;
  report.server_id = ::yac::McpServerId{srv.id};

  {
    std::ostringstream s;
    s << "id:        " << srv.id << "\n";
    s << "transport: " << srv.transport << "\n";
    if (!srv.command.empty()) {
      s << "command:   " << srv.command << "\n";
    }
    if (!srv.url.empty()) {
      s << "url:       " << srv.url << "\n";
    }
    s << "enabled:   " << (srv.enabled ? "true" : "false") << "\n";
    report.status = s.str();
  }

  {
    std::ostringstream s;
    const auto token_json = GetTokenStore().Get(server_id);
    if (!token_json.has_value()) {
      s << "token: not present\n";
    } else {
      s << "token: present\n";
      try {
        const auto j = Json::parse(*token_json);
        if (j.contains("expires_at") && j["expires_at"].is_number_integer()) {
          const auto epoch = j["expires_at"].get<long long>();
          s << "expiry: " << FormatTokenExpiry(epoch) << "\n";
        }
      } catch (...) {
        // SAFETY: token JSON is opaque to this debug command; parse failures
        // surface a benign placeholder rather than aborting the report.
        yac::log::Warn("cli.mcp_admin", "token JSON parse failed: {}",
                       yac::log::DescribeCurrentException());
        s << "expiry: (cannot parse token)\n";
      }
    }
    if (srv.auth.has_value()) {
      if (std::holds_alternative<mcp::McpAuthBearer>(*srv.auth)) {
        s << "auth type: bearer\n";
      } else if (std::holds_alternative<mcp::McpAuthOAuth>(*srv.auth)) {
        s << "auth type: oauth\n";
      }
    } else {
      s << "auth type: none\n";
    }
    report.auth = s.str();
  }

  report.connectivity = [&]() -> std::string {
    std::ostringstream s;
    try {
      const bool ok = opts_.connectivity_test ? opts_.connectivity_test(srv)
                                              : DefaultConnectivityTest(srv);
      s << (ok ? "PASS" : "FAIL") << "\n";
    } catch (const std::exception& e) {
      s << "FAIL (" << e.what() << ")\n";
    }
    return s.str();
  }();

  report.log = [&]() -> std::string {
    std::ostringstream s;
    const auto log_path = DebugLogPath(server_id);
    std::error_code ec;
    if (!std::filesystem::exists(log_path, ec) || ec) {
      s << "(no log file)\n";
    } else {
      const std::string raw = TailLines(log_path, kLogTailLines);
      s << mcp::RedactSecrets(raw);
    }
    return s.str();
  }();

  return report;
}

}  // namespace yac::cli
