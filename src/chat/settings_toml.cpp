#include "chat/settings_toml.hpp"

#include "chat/settings_toml_template.hpp"
#include "mcp/mcp_server_config.hpp"
#include "util/string_util.hpp"

#include <cstddef>
#include <exception>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <toml++/toml.hpp>
#include <unordered_set>
#include <vector>

#ifndef _WIN32
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace yac::chat {

namespace {

void AddError(std::vector<ConfigIssue>& issues, std::string message,
              std::string detail) {
  issues.push_back({.severity = ConfigIssueSeverity::Error,
                    .message = std::move(message),
                    .detail = std::move(detail)});
}

void AddWarning(std::vector<ConfigIssue>& issues, std::string message,
                std::string detail) {
  issues.push_back({.severity = ConfigIssueSeverity::Warning,
                    .message = std::move(message),
                    .detail = std::move(detail)});
}

// Reads a string key from a TOML node; emits an error if the node is the
// wrong type. Returns true when a value was applied.
bool ApplyStringField(const toml::node_view<toml::node>& node,
                      const std::string& key, std::string& target,
                      std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  if (auto* value = node.as_string()) {
    target = value->get();
    return true;
  }
  AddError(issues, "Invalid type for " + key + " in settings.toml",
           "Expected a string.");
  return false;
}

bool ApplyTemperature(const toml::node_view<toml::node>& node, double& target,
                      std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  double parsed = 0.0;
  if (auto* value = node.as_floating_point()) {
    parsed = value->get();
  } else if (auto* value = node.as_integer()) {
    parsed = static_cast<double>(value->get());
  } else {
    AddError(issues, "Invalid type for temperature in settings.toml",
             "Expected a number between 0.0 and 2.0.");
    return false;
  }
  if (parsed < kMinTemperature || parsed > kMaxTemperature) {
    AddError(issues, "Invalid temperature in settings.toml",
             "Value must be between 0.0 and 2.0.");
    return false;
  }
  target = parsed;
  return true;
}

bool ApplyIntegerField(const toml::node_view<toml::node>& node,
                       const std::string& key, int min_value, int max_value,
                       int& target, std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  auto* value = node.as_integer();
  if (value == nullptr) {
    AddError(issues, "Invalid type for " + key + " in settings.toml",
             "Expected an integer.");
    return false;
  }
  const auto parsed = value->get();
  if (parsed < min_value || parsed > max_value) {
    AddError(issues, "Invalid " + key + " in settings.toml",
             "Value must be between " + std::to_string(min_value) + " and " +
                 std::to_string(max_value) + ".");
    return false;
  }
  target = static_cast<int>(parsed);
  return true;
}

bool ApplyBoolField(const toml::node_view<toml::node>& node,
                    const std::string& key, bool& target,
                    std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  if (auto* value = node.as_boolean()) {
    target = value->get();
    return true;
  }
  AddError(issues, "Invalid type for " + key + " in settings.toml",
           "Expected a boolean (true or false).");
  return false;
}

bool ApplyDoubleField(const toml::node_view<toml::node>& node,
                      const std::string& key, double min_value,
                      double max_value, double& target,
                      std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  double parsed = 0.0;
  if (auto* value = node.as_floating_point()) {
    parsed = value->get();
  } else if (auto* value = node.as_integer()) {
    parsed = static_cast<double>(value->get());
  } else {
    AddError(issues, "Invalid type for " + key + " in settings.toml",
             "Expected a number.");
    return false;
  }
  if (parsed < min_value || parsed > max_value) {
    std::ostringstream detail;
    detail << "Value must be between " << min_value << " and " << max_value
           << ".";
    AddError(issues, "Invalid " + key + " in settings.toml", detail.str());
    return false;
  }
  target = parsed;
  return true;
}

bool ApplyStringArray(const toml::node_view<toml::node>& node,
                      const std::string& key, std::vector<std::string>& target,
                      std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  auto* array = node.as_array();
  if (array == nullptr) {
    AddError(issues, "Invalid type for " + key + " in settings.toml",
             "Expected an array of strings.");
    return false;
  }
  std::vector<std::string> values;
  values.reserve(array->size());
  for (const auto& element : *array) {
    if (const auto* string_value = element.as_string()) {
      values.push_back(string_value->get());
    } else {
      AddError(issues, "Invalid element in " + key,
               "Every entry must be a string.");
      return false;
    }
  }
  target = std::move(values);
  return true;
}

void ApplyMcpBearerAuth(toml::table& auth_tbl, mcp::McpServerConfig& server,
                        McpServerFieldSet& server_fields) {
  mcp::McpAuthBearer bearer;
  if (auto* v = auth_tbl["api_key_env"].as_string()) {
    bearer.api_key_env = v->get();
    server_fields.api_key_env = true;
  }
  server.auth = std::move(bearer);
}

void ApplyMcpOAuthAuth(toml::table& auth_tbl, mcp::McpServerConfig& server,
                       std::vector<ConfigIssue>& issues) {
  mcp::McpAuthOAuth oauth;
  if (auto* v = auth_tbl["authorization_url"].as_string()) {
    oauth.authorization_url = v->get();
  }
  if (auto* v = auth_tbl["token_url"].as_string()) {
    oauth.token_url = v->get();
  }
  if (auto* v = auth_tbl["client_id"].as_string()) {
    oauth.client_id = v->get();
  }
  ApplyStringArray(auth_tbl["scopes"], "mcp.servers.auth.scopes", oauth.scopes,
                   issues);
  server.auth = std::move(oauth);
}

void ApplyMcpAuth(toml::table& auth_tbl, mcp::McpServerConfig& server,
                  McpServerFieldSet& server_fields,
                  std::vector<ConfigIssue>& issues) {
  if (auto* type_v = auth_tbl["type"].as_string()) {
    const std::string auth_type = type_v->get();
    if (auth_type == "bearer") {
      ApplyMcpBearerAuth(auth_tbl, server, server_fields);
    } else if (auth_type == "oauth" || auth_type == "oauth2") {
      ApplyMcpOAuthAuth(auth_tbl, server, issues);
    } else {
      AddError(issues, "Unknown mcp.servers.auth.type: " + auth_type,
               "Expected 'bearer', 'oauth', or 'oauth2'.");
    }
    return;
  }

  auto* bearer_tbl = auth_tbl["bearer"].as_table();
  auto* oauth_tbl = auth_tbl["oauth2"].as_table();
  if (bearer_tbl != nullptr && oauth_tbl != nullptr) {
    AddError(issues, "Ambiguous mcp.servers.auth for '" + server.id + "'",
             "Specify only one of auth.bearer or auth.oauth2.");
    return;
  }
  if (bearer_tbl != nullptr) {
    ApplyMcpBearerAuth(*bearer_tbl, server, server_fields);
    return;
  }
  if (oauth_tbl != nullptr) {
    ApplyMcpOAuthAuth(*oauth_tbl, server, issues);
    return;
  }

  AddError(issues, "mcp.servers.auth missing auth type for '" + server.id + "'",
           "Expected auth.type, auth.bearer, or auth.oauth2.");
}

struct TextLine {
  std::string text;
  std::string newline;
};

std::vector<TextLine> SplitPreservingNewlines(std::string_view content) {
  std::vector<TextLine> lines;
  size_t pos = 0;
  while (pos < content.size()) {
    const auto line_end = content.find_first_of("\r\n", pos);
    if (line_end == std::string_view::npos) {
      lines.push_back({std::string(content.substr(pos)), ""});
      break;
    }
    std::string newline;
    size_t next_pos = line_end + 1;
    if (content[line_end] == '\r' && next_pos < content.size() &&
        content[next_pos] == '\n') {
      newline = "\r\n";
      ++next_pos;
    } else {
      newline = content[line_end];
    }
    lines.push_back(
        {std::string(content.substr(pos, line_end - pos)), newline});
    pos = next_pos;
  }
  return lines;
}

std::string DetectNewline(const std::vector<TextLine>& lines) {
  for (const auto& line : lines) {
    if (!line.newline.empty()) {
      return line.newline;
    }
  }
  return "\n";
}

std::optional<std::string> ParseTableHeader(std::string_view line) {
  line = ::yac::util::TrimSv(line);
  if (line.empty() || line.front() != '[' || line.starts_with("[[")) {
    return std::nullopt;
  }
  const auto close = line.find(']');
  if (close == std::string_view::npos) {
    return std::nullopt;
  }
  const auto rest = ::yac::util::TrimSv(line.substr(close + 1));
  if (!rest.empty() && rest.front() != '#') {
    return std::nullopt;
  }
  return std::string(::yac::util::TrimSv(line.substr(1, close - 1)));
}

bool IsKeyAssignment(std::string_view line, std::string_view key) {
  line = ::yac::util::TrimLeftSv(line);
  if (line.empty() || line.front() == '#' || !line.starts_with(key)) {
    return false;
  }
  const auto rest = ::yac::util::TrimLeftSv(line.substr(key.size()));
  return !rest.empty() && rest.front() == '=';
}

std::string QuoteTomlString(std::string_view value) {
  std::ostringstream output;
  output << '"';
  for (const unsigned char ch : value) {
    switch (ch) {
      case '\\':
        output << "\\\\";
        break;
      case '"':
        output << "\\\"";
        break;
      case '\b':
        output << "\\b";
        break;
      case '\t':
        output << "\\t";
        break;
      case '\n':
        output << "\\n";
        break;
      case '\f':
        output << "\\f";
        break;
      case '\r':
        output << "\\r";
        break;
      default:
        if (ch < 0x20) {
          output << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
                 << static_cast<int>(ch) << std::dec;
        } else {
          output << static_cast<char>(ch);
        }
    }
  }
  output << '"';
  return output.str();
}

std::string JoinLines(const std::vector<TextLine>& lines) {
  std::string output;
  for (const auto& line : lines) {
    output += line.text;
    output += line.newline;
  }
  return output;
}

bool ReadTextFile(const std::filesystem::path& path, std::string& content,
                  std::vector<ConfigIssue>& issues) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    AddWarning(issues, "Failed to read " + path.string(),
               "Could not open file.");
    return false;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  if (input.bad()) {
    AddWarning(issues, "Failed to read " + path.string(),
               "I/O error while reading file.");
    return false;
  }
  content = buffer.str();
  return true;
}

bool WriteTextFile(const std::filesystem::path& path, std::string_view content,
                   std::vector<ConfigIssue>& issues) {
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    AddWarning(issues, "Failed to write " + path.string(),
               "Could not open file for writing.");
    return false;
  }
  output << content;
  output.close();
  if (!output) {
    AddWarning(issues, "Failed to write " + path.string(),
               "I/O error while writing file.");
    return false;
  }
  return true;
}

bool ValidateEditableSettingsToml(const std::filesystem::path& path,
                                  std::vector<ConfigIssue>& issues) {
  toml::table table;
  try {
    table = toml::parse_file(path.string());
  } catch (const toml::parse_error& error) {
    AddError(issues, "Failed to parse settings.toml",
             std::string(error.description()));
    return false;
  } catch (const std::exception& error) {
    AddError(issues, "Failed to read settings.toml", error.what());
    return false;
  }

  const auto theme_section = table["theme"];
  if (theme_section && !theme_section.is_table()) {
    AddError(issues, "Invalid type for [theme] in settings.toml",
             "Expected a table.");
    return false;
  }
  if (theme_section.is_table()) {
    const auto theme_name = theme_section["name"];
    if (theme_name && !theme_name.is_string()) {
      AddError(issues, "Invalid type for theme.name in settings.toml",
               "Expected a string.");
      return false;
    }
  }
  return true;
}

bool ValidateGeneratedSettingsToml(std::string_view content,
                                   const std::filesystem::path& path,
                                   std::vector<ConfigIssue>& issues) {
  try {
    static_cast<void>(toml::parse(content, path.string()));
    return true;
  } catch (const toml::parse_error& error) {
    AddError(issues, "Generated invalid settings.toml",
             std::string(error.description()));
  } catch (const std::exception& error) {
    AddError(issues, "Generated invalid settings.toml", error.what());
  }
  return false;
}

std::string WithThemeName(std::string_view content,
                          std::string_view theme_name) {
  auto lines = SplitPreservingNewlines(content);
  const auto newline = DetectNewline(lines);
  const auto assignment = "name = " + QuoteTomlString(theme_name);

  std::optional<size_t> theme_start;
  for (size_t i = 0; i < lines.size(); ++i) {
    const auto table_name = ParseTableHeader(lines[i].text);
    if (table_name == "theme") {
      theme_start = i;
      break;
    }
  }

  if (!theme_start.has_value()) {
    if (!lines.empty()) {
      if (lines.back().newline.empty()) {
        lines.back().newline = newline;
      }
      if (!lines.back().text.empty()) {
        lines.push_back({"", newline});
      }
    }
    lines.push_back({"[theme]", newline});
    lines.push_back({assignment, newline});
    return JoinLines(lines);
  }

  size_t theme_end = lines.size();
  for (size_t i = *theme_start + 1; i < lines.size(); ++i) {
    if (ParseTableHeader(lines[i].text).has_value()) {
      theme_end = i;
      break;
    }
  }

  for (size_t i = *theme_start + 1; i < theme_end; ++i) {
    if (IsKeyAssignment(lines[i].text, "name")) {
      lines[i].text = assignment;
      return JoinLines(lines);
    }
  }

  lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(*theme_start + 1),
               TextLine{.text = assignment, .newline = newline});
  return JoinLines(lines);
}

}  // namespace

ChatConfigFieldSet LoadSettingsFromToml(const std::filesystem::path& path,
                                        ChatConfig& config,
                                        std::vector<ConfigIssue>& issues) {
  ChatConfigFieldSet fields;
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return fields;
  }

  toml::table table;
  try {
    table = toml::parse_file(path.string());
  } catch (const toml::parse_error& error) {
    AddError(issues, "Failed to parse settings.toml",
             std::string(error.description()));
    return fields;
  } catch (const std::exception& error) {
    AddError(issues, "Failed to read settings.toml", error.what());
    return fields;
  }

  fields.temperature =
      ApplyTemperature(table["temperature"], config.temperature, issues);
  fields.max_tool_rounds = ApplyIntegerField(
      table["max_tool_rounds"], "max_tool_rounds", kMinToolRoundLimit,
      kMaxToolRoundLimit, config.max_tool_rounds, issues);

  if (auto* system_prompt = table["system_prompt"].as_string()) {
    config.system_prompt = system_prompt->get();
    fields.system_prompt = true;
  } else if (table.contains("system_prompt")) {
    AddError(issues, "Invalid type for system_prompt in settings.toml",
             "Expected a string.");
  }

  fields.workspace_root = ApplyStringField(
      table["workspace_root"], "workspace_root", config.workspace_root, issues);

  const auto provider = table["provider"];
  if (provider.is_table()) {
    fields.provider_id = ApplyStringField(provider["id"], "provider.id",
                                          config.provider_id, issues);
    fields.model = ApplyStringField(provider["model"], "provider.model",
                                    config.model, issues);
    fields.base_url = ApplyStringField(
        provider["base_url"], "provider.base_url", config.base_url, issues);
    fields.api_key_env =
        ApplyStringField(provider["api_key_env"], "provider.api_key_env",
                         config.api_key_env, issues);
    fields.api_key = ApplyStringField(provider["api_key"], "provider.api_key",
                                      config.api_key, issues);
  } else if (table.contains("provider")) {
    AddError(issues, "Invalid type for [provider] in settings.toml",
             "Expected a table.");
  }

  const auto clangd = table["lsp"]["clangd"];
  if (clangd.is_table()) {
    fields.lsp_clangd_command =
        ApplyStringField(clangd["command"], "lsp.clangd.command",
                         config.lsp_clangd_command, issues);
    fields.lsp_clangd_args = ApplyStringArray(clangd["args"], "lsp.clangd.args",
                                              config.lsp_clangd_args, issues);
  } else if (table.contains("lsp")) {
    if (const auto lsp = table["lsp"]; !lsp.is_table()) {
      AddError(issues, "Invalid type for [lsp] in settings.toml",
               "Expected a table.");
    }
  }

  const auto theme_section = table["theme"];
  if (theme_section.is_table()) {
    ApplyBoolField(theme_section["sync_terminal_background"],
                   "theme.sync_terminal_background",
                   config.sync_terminal_background, issues);
    if (ApplyStringField(theme_section["name"], "theme.name", config.theme_name,
                         issues)) {
      fields.theme_name = true;
      if (config.theme_name.empty()) {
        AddWarning(issues, "theme.name is empty in settings.toml",
                   "Falling back to default theme 'vivid'.");
        config.theme_name = "vivid";
        fields.theme_name = false;
      }
    }
    if (ApplyStringField(theme_section["density"], "theme.density",
                         config.theme_density, issues)) {
      fields.theme_density = true;
      if (config.theme_density != "compact" &&
          config.theme_density != "comfortable") {
        AddWarning(issues, "Unknown theme.density in settings.toml",
                   "Falling back to default theme density 'comfortable'.");
        config.theme_density = "comfortable";
      }
    }
  } else if (table.contains("theme")) {
    AddError(issues, "Invalid type for [theme] in settings.toml",
             "Expected a table.");
  }

  const auto compact_section = table["compact"];
  if (compact_section.is_table()) {
    ApplyBoolField(compact_section["auto_enabled"], "compact.auto_enabled",
                   config.auto_compact_enabled, issues);
    ApplyDoubleField(compact_section["threshold"], "compact.threshold",
                     kMinAutoCompactThreshold, kMaxAutoCompactThreshold,
                     config.auto_compact_threshold, issues);
    ApplyIntegerField(compact_section["keep_last"], "compact.keep_last",
                      kMinAutoCompactKeepLast, kMaxAutoCompactKeepLast,
                      config.auto_compact_keep_last, issues);
    if (ApplyStringField(compact_section["mode"], "compact.mode",
                         config.auto_compact_mode, issues)) {
      if (config.auto_compact_mode != "summarize" &&
          config.auto_compact_mode != "truncate") {
        AddError(issues, "Invalid compact.mode in settings.toml",
                 "Value must be 'summarize' or 'truncate'.");
        config.auto_compact_mode = "summarize";
      }
    }
  } else if (table.contains("compact")) {
    AddError(issues, "Invalid type for [compact] in settings.toml",
             "Expected a table.");
  }

  const auto mcp_section = table["mcp"];
  if (mcp_section.is_table()) {
    if (const auto* v = mcp_section["result_max_bytes"].as_integer()) {
      const auto raw = v->get();
      if (raw <= 0) {
        AddError(issues, "Invalid mcp.result_max_bytes in settings.toml",
                 "Must be a positive integer.");
      } else {
        config.mcp.result_max_bytes = static_cast<uintmax_t>(raw);
      }
    } else if (mcp_section["result_max_bytes"]) {
      AddError(issues, "Invalid type for mcp.result_max_bytes in settings.toml",
               "Expected an integer.");
    }

    const auto servers_node = mcp_section["servers"];
    if (servers_node.is_array()) {
      auto* servers_arr = servers_node.as_array();
      std::unordered_set<std::string> seen_ids;
      for (auto& server_elem : *servers_arr) {
        auto* server_tbl = server_elem.as_table();
        if (server_tbl == nullptr) {
          AddError(issues, "Invalid element in [[mcp.servers]]",
                   "Expected a table.");
          continue;
        }
        mcp::McpServerConfig srv;
        McpServerFieldSet server_fields;

        if (auto* v = (*server_tbl)["id"].as_string()) {
          srv.id = v->get();
        }
        if (srv.id.empty()) {
          AddError(issues, "Missing or empty id in [[mcp.servers]]",
                   "Each server entry must have a non-empty id.");
          continue;
        }
        if (seen_ids.contains(srv.id)) {
          AddError(issues, "Duplicate mcp.servers id: " + srv.id,
                   "duplicate server id; keeping first entry.");
          continue;
        }
        seen_ids.insert(srv.id);

        if (auto* v = (*server_tbl)["transport"].as_string()) {
          srv.transport = v->get();
        }
        if (auto* v = (*server_tbl)["command"].as_string()) {
          srv.command = v->get();
          server_fields.command = true;
        }
        ApplyStringArray((*server_tbl)["args"], "mcp.servers.args", srv.args,
                         issues);
        if ((*server_tbl)["args"]) {
          server_fields.args = true;
        }
        if (auto* v = (*server_tbl)["url"].as_string()) {
          srv.url = v->get();
          server_fields.url = true;
        }

        if (auto* env_tbl = (*server_tbl)["env"].as_table()) {
          for (auto& [k, v] : *env_tbl) {
            if (auto* sv = v.as_string()) {
              srv.env.emplace(std::string(k), sv->get());
            } else {
              AddError(issues, "Invalid value in mcp.servers.env",
                       "All env values must be strings.");
            }
          }
        } else if ((*server_tbl)["env"]) {
          AddError(issues, "Invalid type for mcp.servers.env",
                   "Expected a table of string values.");
        }

        const auto& presets = mcp::McpServerPresets();
        if (auto it = presets.find(srv.id); it != presets.end()) {
          const auto& preset = it->second;
          if (srv.transport.empty()) {
            srv.transport = preset.transport;
          }
          if (srv.command.empty()) {
            srv.command = preset.command;
          }
          if (srv.args.empty()) {
            srv.args = preset.args;
          }
          if (srv.url.empty()) {
            srv.url = preset.url;
          }
        }

        if (srv.transport != "stdio" && srv.transport != "http") {
          AddError(issues, "Invalid transport for mcp server '" + srv.id + "'",
                   "Must be 'stdio' or 'http'.");
          continue;
        }
        if (srv.transport == "stdio" && srv.command.empty()) {
          AddError(issues, "mcp server '" + srv.id + "' requires command",
                   "transport='stdio' servers must specify command.");
          continue;
        }
        if (srv.transport == "http" && srv.url.empty()) {
          AddError(issues, "mcp server '" + srv.id + "' requires url",
                   "transport='http' servers must specify url.");
          continue;
        }

        if (auto* v = (*server_tbl)["enabled"].as_boolean()) {
          srv.enabled = v->get();
          server_fields.enabled = true;
        }
        if (auto* v = (*server_tbl)["requires_approval"].as_boolean()) {
          srv.requires_approval = v->get();
        }
        if (auto* v = (*server_tbl)["auto_start"].as_boolean()) {
          srv.auto_start = v->get();
        }
        ApplyStringArray((*server_tbl)["approval_required_tools"],
                         "mcp.servers.approval_required_tools",
                         srv.approval_required_tools, issues);

        if (auto* auth_tbl = (*server_tbl)["auth"].as_table()) {
          ApplyMcpAuth(*auth_tbl, srv, server_fields, issues);
        } else if ((*server_tbl)["auth"]) {
          AddError(issues,
                   "Invalid type for mcp.servers.auth for '" + srv.id + "'",
                   "Expected a table.");
        }

        if (auto* headers_tbl = (*server_tbl)["headers"].as_table()) {
          for (auto& [k, v] : *headers_tbl) {
            if (auto* sv = v.as_string()) {
              srv.headers.emplace(std::string(k), sv->get());
            } else {
              AddError(
                  issues,
                  "Invalid value in mcp.servers.headers for '" + srv.id + "'",
                  "All header values must be strings.");
            }
          }
        } else if ((*server_tbl)["headers"]) {
          AddError(issues,
                   "Invalid type for mcp.servers.headers for '" + srv.id + "'",
                   "Expected a table of string values.");
        }

        fields.mcp_servers.emplace(srv.id, std::move(server_fields));
        config.mcp.servers.push_back(std::move(srv));
      }
    } else if (mcp_section["servers"]) {
      AddError(issues, "Invalid type for [[mcp.servers]] in settings.toml",
               "Expected an array of tables.");
    }
  } else if (table.contains("mcp")) {
    AddError(issues, "Invalid type for [mcp] in settings.toml",
             "Expected a table.");
  }

  return fields;
}

void WriteDefaultSettingsToml(const std::filesystem::path& path,
                              std::vector<ConfigIssue>& issues) {
  std::error_code ec;
  const auto parent = path.parent_path();
  if (!parent.empty()) {
    std::filesystem::create_directories(parent, ec);
    if (ec) {
      AddWarning(issues, "Failed to create " + parent.string(), ec.message());
      return;
    }
#ifndef _WIN32
    std::filesystem::permissions(parent, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, ec);
    if (ec) {
      AddWarning(issues, "Failed to set permissions on " + parent.string(),
                 ec.message());
      ec.clear();
    }
#endif
  }

#ifndef _WIN32
  // Create atomically with 0600 so the file never exists with umask-default
  // permissions. O_EXCL also means we will not clobber a file created by a
  // racing process between the caller's existence check and this call.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC,
                        S_IRUSR | S_IWUSR);
  if (fd < 0) {
    if (errno == EEXIST) {
      // Another writer beat us to it; the caller's "create if missing"
      // semantic is satisfied, so treat as success.
      return;
    }
    AddWarning(issues, "Failed to create " + path.string(),
               std::strerror(errno));
    return;
  }
  const std::string_view body = kDefaultSettingsToml;
  size_t written = 0;
  while (written < body.size()) {
    const auto bytes =
        ::write(fd, body.data() + written, body.size() - written);
    if (bytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      const auto err = std::string(std::strerror(errno));
      ::close(fd);
      AddWarning(issues, "Failed to write " + path.string(), err);
      return;
    }
    written += static_cast<size_t>(bytes);
  }
  if (::close(fd) != 0) {
    AddWarning(issues, "Failed to close " + path.string(),
               std::strerror(errno));
  }
#else
  std::ofstream output(path, std::ios::trunc);
  if (!output) {
    AddWarning(issues, "Failed to create " + path.string(),
               "YAC will continue with built-in defaults.");
    return;
  }
  output << kDefaultSettingsToml;
  output.close();
  if (!output) {
    AddWarning(issues, "Failed to write " + path.string(),
               "YAC will continue with built-in defaults.");
    return;
  }
#endif
}

bool SaveThemeNameToSettingsToml(const std::filesystem::path& path,
                                 std::string_view theme_name,
                                 std::vector<ConfigIssue>& issues) {
  if (theme_name.empty()) {
    AddError(issues, "Cannot save empty theme name",
             "Choose a named theme preset before saving.");
    return false;
  }

  std::error_code ec;
  const bool exists = std::filesystem::exists(path, ec) && !ec;
  if (ec) {
    AddWarning(issues, "Failed to inspect " + path.string(), ec.message());
    return false;
  }
  if (!exists) {
    WriteDefaultSettingsToml(path, issues);
    ec.clear();
    if (!std::filesystem::exists(path, ec) || ec) {
      if (ec) {
        AddWarning(issues, "Failed to inspect " + path.string(), ec.message());
      }
      return false;
    }
  }

  if (!ValidateEditableSettingsToml(path, issues)) {
    return false;
  }

  std::string content;
  if (!ReadTextFile(path, content, issues)) {
    return false;
  }

  const auto updated = WithThemeName(content, theme_name);
  if (!ValidateGeneratedSettingsToml(updated, path, issues)) {
    return false;
  }

  return WriteTextFile(path, updated, issues);
}

}  // namespace yac::chat
