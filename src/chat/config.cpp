#include "chat/config.hpp"

#include "chat/config_paths.hpp"
#include "chat/settings_registry.hpp"
#include "chat/settings_toml.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yac::chat {

namespace {

std::optional<std::string> GetEnv(const char* name) {
  if (const char* value = std::getenv(name)) {
    return std::string(value);
  }
  return std::nullopt;
}

std::string LowerAscii(std::string value) {
  std::ranges::transform(value, value.begin(), [](unsigned char ch) {
    return static_cast<char>(std::tolower(ch));
  });
  return value;
}

bool ParseEnvBool(const std::string& value) {
  const std::string normalized = LowerAscii(value);
  return !(normalized == "0" || normalized == "false" || normalized == "no" ||
           normalized == "off");
}

const SettingMetadata& Metadata(std::string_view key) {
  return *FindSettingsRegistryRecord(key);
}

std::string FormatNumber(double value) {
  std::ostringstream output;
  output << value;
  return output.str();
}

std::string JoinAllowedValues(std::span<const std::string_view> values) {
  std::string joined;
  for (size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      joined += i + 1 == values.size() ? " or " : ", ";
    }
    joined += "'";
    joined += values[i];
    joined += "'";
  }
  return joined;
}

std::string ValidationDetail(const SettingValidationMetadata& validation) {
  switch (validation.type) {
    case SettingValidationType::NumberRange:
      return "must be between " + FormatNumber(validation.min_number) +
             " and " + FormatNumber(validation.max_number);
    case SettingValidationType::IntegerRange:
      return "must be between " + std::to_string(validation.min_integer) +
             " and " + std::to_string(validation.max_integer);
    case SettingValidationType::PositiveInteger:
      return "must be a positive integer";
    case SettingValidationType::StringEnum:
      return "must be " + JoinAllowedValues(validation.allowed_values);
    case SettingValidationType::None:
    case SettingValidationType::Required:
      return {};
  }
  return {};
}

void ValidateEnvNumber(std::string_view key, std::string_view env_var,
                       double parsed) {
  const auto& validation = Metadata(key).validation;
  if (validation.type == SettingValidationType::NumberRange &&
      (parsed < validation.min_number || parsed > validation.max_number)) {
    throw std::out_of_range(std::string(env_var) + " " +
                            ValidationDetail(validation));
  }
}

void ValidateEnvInteger(std::string_view key, std::string_view env_var,
                        int64_t parsed) {
  const auto& validation = Metadata(key).validation;
  if (validation.type == SettingValidationType::IntegerRange &&
      (parsed < validation.min_integer || parsed > validation.max_integer)) {
    throw std::out_of_range(std::string(env_var) + " " +
                            ValidationDetail(validation));
  }
  if (validation.type == SettingValidationType::PositiveInteger &&
      parsed <= 0) {
    throw std::invalid_argument(std::string(env_var) + " " +
                                ValidationDetail(validation));
  }
}

double ParseEnvNumber(const std::string& value, std::string_view key,
                      std::string_view env_var) {
  size_t consumed = 0;
  const double parsed = std::stod(value, &consumed);
  if (consumed != value.size()) {
    throw std::invalid_argument(std::string(env_var) + " must be a number");
  }
  ValidateEnvNumber(key, env_var, parsed);
  return parsed;
}

int ParseEnvInteger(const std::string& value, std::string_view key,
                    std::string_view env_var) {
  size_t consumed = 0;
  const int parsed = std::stoi(value, &consumed);
  if (consumed != value.size()) {
    throw std::invalid_argument(std::string(env_var) + " must be an integer");
  }
  ValidateEnvInteger(key, env_var, parsed);
  return parsed;
}

std::string ParseEnvStringEnum(const std::string& value, std::string_view key,
                               std::string_view env_var) {
  const auto& validation = Metadata(key).validation;
  for (const auto allowed : validation.allowed_values) {
    if (value == allowed) {
      return value;
    }
  }
  throw std::invalid_argument(std::string(env_var) + " " +
                              ValidationDetail(validation));
}

uintmax_t ParsePositiveUintmax(const std::string& value, std::string_view key,
                               std::string_view env_var) {
  if (value.empty() || !std::ranges::all_of(value, [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    throw std::invalid_argument(std::string(env_var) + " " +
                                ValidationDetail(Metadata(key).validation));
  }
  size_t consumed = 0;
  const auto parsed = std::stoull(value, &consumed);
  if (consumed != value.size()) {
    throw std::invalid_argument(std::string(env_var) + " " +
                                ValidationDetail(Metadata(key).validation));
  }
  if (parsed > std::numeric_limits<uintmax_t>::max()) {
    throw std::out_of_range(std::string(env_var) + " is too large");
  }
  if (parsed == 0) {
    throw std::invalid_argument(std::string(env_var) + " " +
                                ValidationDetail(Metadata(key).validation));
  }
  return static_cast<uintmax_t>(parsed);
}

void SkipJsonWhitespace(std::string_view value, size_t& pos) {
  while (pos < value.size() &&
         std::isspace(static_cast<unsigned char>(value[pos])) != 0) {
    ++pos;
  }
}

bool ParseJsonString(std::string_view value, size_t& pos, std::string& out) {
  if (pos >= value.size() || value[pos] != '"') {
    return false;
  }
  ++pos;
  out.clear();
  while (pos < value.size()) {
    const char ch = value[pos++];
    if (ch == '"') {
      return true;
    }
    if (ch != '\\') {
      out.push_back(ch);
      continue;
    }
    if (pos >= value.size()) {
      return false;
    }
    const char escaped = value[pos++];
    switch (escaped) {
      case '"':
      case '\\':
      case '/':
        out.push_back(escaped);
        break;
      case 'b':
        out.push_back('\b');
        break;
      case 'f':
        out.push_back('\f');
        break;
      case 'n':
        out.push_back('\n');
        break;
      case 'r':
        out.push_back('\r');
        break;
      case 't':
        out.push_back('\t');
        break;
      default:
        return false;
    }
  }
  return false;
}

void AddJsonMapError(std::string_view env_var, std::string detail,
                     std::vector<ConfigIssue>& issues) {
  issues.push_back({.severity = ConfigIssueSeverity::Error,
                    .message = "Invalid " + std::string(env_var),
                    .detail = std::move(detail)});
}

std::optional<std::unordered_map<std::string, std::string>> ParseStringMapJson(
    std::string_view env_var, const std::string& value,
    std::vector<ConfigIssue>& issues) {
  std::unordered_map<std::string, std::string> parsed;
  size_t pos = 0;
  SkipJsonWhitespace(value, pos);
  if (pos >= value.size() || value[pos] != '{') {
    AddJsonMapError(env_var, "Expected a JSON object of string values.",
                    issues);
    return std::nullopt;
  }
  ++pos;
  SkipJsonWhitespace(value, pos);
  if (pos < value.size() && value[pos] == '}') {
    ++pos;
    SkipJsonWhitespace(value, pos);
    if (pos == value.size()) {
      return parsed;
    }
    AddJsonMapError(env_var, "Unexpected trailing characters.", issues);
    return std::nullopt;
  }

  while (pos < value.size()) {
    std::string key;
    std::string map_value;
    if (!ParseJsonString(value, pos, key)) {
      AddJsonMapError(env_var, "Expected JSON object string keys.", issues);
      return std::nullopt;
    }
    SkipJsonWhitespace(value, pos);
    if (pos >= value.size() || value[pos] != ':') {
      AddJsonMapError(env_var, "Expected ':' after JSON object key.", issues);
      return std::nullopt;
    }
    ++pos;
    SkipJsonWhitespace(value, pos);
    if (!ParseJsonString(value, pos, map_value)) {
      AddJsonMapError(env_var, "All JSON object values must be strings.",
                      issues);
      return std::nullopt;
    }
    parsed[std::move(key)] = std::move(map_value);
    SkipJsonWhitespace(value, pos);
    if (pos < value.size() && value[pos] == ',') {
      ++pos;
      SkipJsonWhitespace(value, pos);
      continue;
    }
    if (pos < value.size() && value[pos] == '}') {
      ++pos;
      SkipJsonWhitespace(value, pos);
      if (pos == value.size()) {
        return parsed;
      }
      AddJsonMapError(env_var, "Unexpected trailing characters.", issues);
      return std::nullopt;
    }
    AddJsonMapError(env_var, "Expected ',' or '}' in JSON object.", issues);
    return std::nullopt;
  }

  AddJsonMapError(env_var, "Unterminated JSON object.", issues);
  return std::nullopt;
}

mcp::McpAuthBearer& EnsureBearerAuth(mcp::McpServerConfig& server) {
  if (!server.auth.has_value() ||
      !std::holds_alternative<mcp::McpAuthBearer>(*server.auth)) {
    server.auth = mcp::McpAuthBearer{};
  }
  return std::get<mcp::McpAuthBearer>(*server.auth);
}

mcp::McpAuthOAuth& EnsureOAuthAuth(mcp::McpServerConfig& server) {
  if (!server.auth.has_value() ||
      !std::holds_alternative<mcp::McpAuthOAuth>(*server.auth)) {
    server.auth = mcp::McpAuthOAuth{};
  }
  return std::get<mcp::McpAuthOAuth>(*server.auth);
}

std::vector<std::string> SplitArgs(const std::string& value) {
  std::istringstream stream(value);
  std::vector<std::string> args;
  std::string arg;
  while (stream >> arg) {
    args.push_back(arg);
  }
  return args;
}

std::string UpperSnakeCase(const std::string& value) {
  std::string result;
  result.reserve(value.size());
  for (unsigned char ch : value) {
    if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
        (ch >= '0' && ch <= '9')) {
      result.push_back(static_cast<char>(std::toupper(ch)));
    } else {
      result.push_back('_');
    }
  }
  return result;
}

struct ProviderPreset {
  std::string model;
  std::string base_url;
  std::string api_key_env;
};

const std::unordered_map<std::string, ProviderPreset>& ProviderPresets() {
  static const std::unordered_map<std::string, ProviderPreset> presets = {
      {"openai",
       {.model = "gpt-4o-mini",
        .base_url = "https://api.openai.com/v1/",
        .api_key_env = "OPENAI_API_KEY"}},
      {"zai",
       {.model = "glm-5.1",
        .base_url = "https://api.z.ai/api/coding/paas/v4",
        .api_key_env = "ZAI_API_KEY"}},
      {"bedrock",
       {.model = "anthropic.claude-3-5-haiku-20241022-v1:0",
        .base_url = "",
        .api_key_env = ""}},
  };
  return presets;
}

// Apply a provider preset, but only to fields the user did not already set
// explicitly (via TOML or env vars). This lets `provider.id = "zai"` alone
// pick up glm-5.1 / z.ai URL / ZAI_API_KEY, while a TOML that also sets
// provider.model keeps the user-supplied model.
void ApplyProviderDefaults(ChatConfig& config,
                           const ChatConfigFieldSet& explicit_fields) {
  const auto& presets = ProviderPresets();
  auto it = presets.find(config.provider_id.value);
  if (it == presets.end()) {
    return;
  }
  if (!explicit_fields.model) {
    config.model = ModelId{it->second.model};
  }
  if (!explicit_fields.base_url) {
    config.base_url = it->second.base_url;
  }
  if (!explicit_fields.api_key_env) {
    config.api_key_env = it->second.api_key_env;
  }
}

void ApplyEnvOverrides(ChatConfig& config, ChatConfigFieldSet& fields,
                       std::vector<ConfigIssue>& issues) {
  if (auto val = GetEnv("YAC_PROVIDER")) {
    config.provider_id = ProviderId{std::move(*val)};
    // Changing provider_id invalidates preset-derived fields unless the TOML
    // (or a prior env var) already fixed them.
    fields.provider_id = true;
    ApplyProviderDefaults(config, fields);
  }
  if (auto val = GetEnv("YAC_MODEL")) {
    config.model = ModelId{std::move(*val)};
    fields.model = true;
  }
  if (auto val = GetEnv("YAC_BASE_URL")) {
    config.base_url = std::move(*val);
    fields.base_url = true;
  }
  if (auto val = GetEnv("YAC_TEMPERATURE")) {
    try {
      config.temperature =
          ParseEnvNumber(*val, "temperature", "YAC_TEMPERATURE");
      fields.temperature = true;
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_TEMPERATURE",
                        .detail = error.what()});
    }
  }
  if (auto val = GetEnv("YAC_CONTEXT_WINDOW")) {
    try {
      config.context_window = ParseEnvInteger(*val, "provider.context_window",
                                              "YAC_CONTEXT_WINDOW");
      fields.context_window = true;
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_CONTEXT_WINDOW",
                        .detail = error.what()});
    }
  }
  if (auto val = GetEnv("YAC_API_KEY_ENV")) {
    config.api_key_env = std::move(*val);
    fields.api_key_env = true;
  }
  if (auto val = GetEnv("YAC_SYSTEM_PROMPT")) {
    config.system_prompt = std::move(*val);
    fields.system_prompt = true;
  }
  if (auto val = GetEnv("YAC_WORKSPACE_ROOT")) {
    config.workspace_root = std::move(*val);
    fields.workspace_root = true;
  }
  if (auto val = GetEnv("YAC_LSP_CLANGD_COMMAND")) {
    config.lsp_clangd_command = std::move(*val);
    fields.lsp_clangd_command = true;
  }
  if (auto val = GetEnv("YAC_LSP_CLANGD_ARGS")) {
    config.lsp_clangd_args = SplitArgs(*val);
    fields.lsp_clangd_args = true;
  }
  if (auto val = GetEnv("YAC_SYNC_TERMINAL_BACKGROUND")) {
    config.sync_terminal_background = ParseEnvBool(*val);
  }
  if (auto val = GetEnv("YAC_THEME_NAME")) {
    config.theme_name = std::move(*val);
    fields.theme_name = true;
  }
  if (auto val = GetEnv("YAC_THEME_DENSITY")) {
    config.theme_density = std::move(*val);
    fields.theme_density = true;
  }
  if (auto val = GetEnv("YAC_COMPACT_AUTO_ENABLED")) {
    config.auto_compact_enabled = ParseEnvBool(*val);
  }
  if (auto val = GetEnv("YAC_COMPACT_THRESHOLD")) {
    try {
      config.auto_compact_threshold =
          ParseEnvNumber(*val, "compact.threshold", "YAC_COMPACT_THRESHOLD");
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_COMPACT_THRESHOLD",
                        .detail = error.what()});
    }
  }
  if (auto val = GetEnv("YAC_COMPACT_KEEP_LAST")) {
    try {
      config.auto_compact_keep_last =
          ParseEnvInteger(*val, "compact.keep_last", "YAC_COMPACT_KEEP_LAST");
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_COMPACT_KEEP_LAST",
                        .detail = error.what()});
    }
  }
  if (auto val = GetEnv("YAC_COMPACT_MODE")) {
    try {
      config.auto_compact_mode =
          ParseEnvStringEnum(*val, "compact.mode", "YAC_COMPACT_MODE");
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_COMPACT_MODE",
                        .detail = error.what()});
    }
  }

  if (auto val = GetEnv("YAC_MCP_RESULT_MAX_BYTES")) {
    try {
      config.mcp.result_max_bytes = ParsePositiveUintmax(
          *val, "mcp.result_max_bytes", "YAC_MCP_RESULT_MAX_BYTES");
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_MCP_RESULT_MAX_BYTES",
                        .detail = error.what()});
    }
  }

  // Bedrock-specific env overrides
  if (config.provider_id.value == "bedrock") {
    if (!config.options.contains("region")) {
      config.options["region"] = "us-east-1";
    }
    if (!config.options.contains("max_tokens")) {
      config.options["max_tokens"] =
          std::to_string(Metadata("provider.options.max_tokens")
                             .runtime_default.integer_value);
    }
    if (auto val = GetEnv("YAC_BEDROCK_REGION")) {
      config.options["region"] = std::move(*val);
    } else if (auto val = GetEnv("AWS_REGION")) {
      config.options["region"] = std::move(*val);
    }
    if (auto val = GetEnv("YAC_BEDROCK_PROFILE")) {
      config.options["profile"] = std::move(*val);
    }
    if (auto val = GetEnv("YAC_BEDROCK_ENDPOINT_OVERRIDE")) {
      config.options["endpoint_override"] = std::move(*val);
    }
    if (auto val = GetEnv("YAC_BEDROCK_CREDENTIAL_REFRESH_COMMAND")) {
      config.options["credential_refresh_command"] = std::move(*val);
    }
    if (auto val = GetEnv("YAC_BEDROCK_MAX_TOKENS")) {
      config.options["max_tokens"] = std::move(*val);
    }
    try {
      const std::string& raw = config.options.at("max_tokens");
      static_cast<void>(ParseEnvInteger(raw, "provider.options.max_tokens",
                                        "Bedrock max_tokens"));
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid Bedrock max_tokens",
                        .detail = error.what()});
      config.options["max_tokens"] =
          std::to_string(Metadata("provider.options.max_tokens")
                             .runtime_default.integer_value);
    }
  }

  std::unordered_map<std::string, int> normalized_server_id_counts;
  for (const auto& server : config.mcp.servers) {
    ++normalized_server_id_counts[UpperSnakeCase(server.id)];
  }
  std::vector<std::string> colliding_normalized_server_ids;
  for (const auto& [normalized_id, count] : normalized_server_id_counts) {
    if (count > 1) {
      colliding_normalized_server_ids.push_back(normalized_id);
      issues.push_back(
          {.severity = ConfigIssueSeverity::Error,
           .message = "MCP server ids collide after upper-snake normalization",
           .detail = "YAC_MCP_" + normalized_id +
                     "_* overrides are ambiguous and were ignored."});
    }
  }

  for (auto& server : config.mcp.servers) {
    const std::string normalized_server_id = UpperSnakeCase(server.id);
    if (std::ranges::find(colliding_normalized_server_ids,
                          normalized_server_id) !=
        colliding_normalized_server_ids.end()) {
      continue;
    }
    const std::string server_prefix = "YAC_MCP_" + normalized_server_id + "_";

    if (auto val = GetEnv((server_prefix + "TRANSPORT").c_str())) {
      server.transport = std::move(*val);
    }
    if (auto val = GetEnv((server_prefix + "COMMAND").c_str())) {
      server.command = std::move(*val);
    }
    if (auto val = GetEnv((server_prefix + "ARGS").c_str())) {
      server.args = SplitArgs(*val);
    }
    if (auto val = GetEnv((server_prefix + "URL").c_str())) {
      server.url = std::move(*val);
    }
    if (auto val = GetEnv((server_prefix + "ENABLED").c_str())) {
      server.enabled = ParseEnvBool(*val);
    }
    if (auto val = GetEnv((server_prefix + "AUTO_START").c_str())) {
      server.auto_start = ParseEnvBool(*val);
    }
    if (auto val = GetEnv((server_prefix + "REQUIRES_APPROVAL").c_str())) {
      server.requires_approval = ParseEnvBool(*val);
    }
    if (auto val =
            GetEnv((server_prefix + "APPROVAL_REQUIRED_TOOLS").c_str())) {
      server.approval_required_tools = SplitArgs(*val);
    }
    if (auto val = GetEnv((server_prefix + "ENV_JSON").c_str())) {
      if (auto parsed =
              ParseStringMapJson(server_prefix + "ENV_JSON", *val, issues)) {
        server.env = std::move(*parsed);
      }
    }
    if (auto val = GetEnv((server_prefix + "HEADERS_JSON").c_str())) {
      if (auto parsed = ParseStringMapJson(server_prefix + "HEADERS_JSON", *val,
                                           issues)) {
        server.headers = std::move(*parsed);
      }
    }
    if (auto val = GetEnv((server_prefix + "AUTH_TYPE").c_str())) {
      if (*val == "bearer") {
        EnsureBearerAuth(server);
      } else if (*val == "oauth") {
        EnsureOAuthAuth(server);
      } else {
        issues.push_back({.severity = ConfigIssueSeverity::Error,
                          .message = "Invalid " + server_prefix + "AUTH_TYPE",
                          .detail = "Expected 'bearer' or 'oauth'."});
      }
    }
    if (auto val = GetEnv((server_prefix + "API_KEY_ENV").c_str())) {
      if (server.auth.has_value()) {
        if (auto* bearer = std::get_if<mcp::McpAuthBearer>(&*server.auth)) {
          bearer->api_key_env = std::move(*val);
        }
      }
    }
    if (auto val =
            GetEnv((server_prefix + "OAUTH_AUTHORIZATION_URL").c_str())) {
      EnsureOAuthAuth(server).authorization_url = std::move(*val);
    }
    if (auto val = GetEnv((server_prefix + "OAUTH_TOKEN_URL").c_str())) {
      EnsureOAuthAuth(server).token_url = std::move(*val);
    }
    if (auto val = GetEnv((server_prefix + "OAUTH_CLIENT_ID").c_str())) {
      EnsureOAuthAuth(server).client_id = std::move(*val);
    }
    if (auto val = GetEnv((server_prefix + "OAUTH_SCOPES").c_str())) {
      EnsureOAuthAuth(server).scopes = SplitArgs(*val);
    }
  }
}

void ResolveApiKey(ChatConfig& config, const ChatConfigFieldSet& fields,
                   std::vector<ConfigIssue>& issues) {
  // If settings.toml supplied an api_key directly, keep it. Otherwise fall
  // back to reading the env var named by api_key_env.
  if (!fields.api_key || config.api_key.empty()) {
    if (auto val = GetEnv(config.api_key_env.c_str())) {
      config.api_key = std::move(*val);
    }
  }
  if (config.api_key.empty() && config.provider_id.value != "bedrock" &&
      config.provider_id.value != "openai") {
    issues.push_back({.severity = ConfigIssueSeverity::Warning,
                      .message = config.api_key_env + " is not set",
                      .detail = "Set " + config.api_key_env +
                                " in your environment or in "
                                "~/.yac/settings.toml before sending "
                                "a request."});
  }
}

}  // namespace

ChatConfig LoadChatConfig() {
  return LoadChatConfigResult().config;
}

ChatConfigResult LoadChatConfigResult() {
  std::filesystem::path settings_path;
  try {
    settings_path = GetSettingsPath();
  } catch (const std::exception& error) {
    // Couldn't resolve $HOME — surface it as a warning, keep going with
    // defaults + env overrides so the app still launches.
    ChatConfigResult result;
    auto& config = result.config;
    config.workspace_root = std::filesystem::current_path().string();
    result.issues.push_back({.severity = ConfigIssueSeverity::Warning,
                             .message = "Could not locate ~/.yac",
                             .detail = error.what()});
    ChatConfigFieldSet fields;
    ApplyEnvOverrides(config, fields, result.issues);
    if (fields.provider_id) {
      ApplyProviderDefaults(config, fields);
    }
    ResolveApiKey(config, fields, result.issues);
    return result;
  }
  return LoadChatConfigResultFrom(settings_path, /*create_if_missing=*/true);
}

ChatConfigResult LoadChatConfigResultFrom(
    const std::filesystem::path& settings_path, bool create_if_missing) {
  ChatConfigResult result;
  auto& config = result.config;
  config.workspace_root = std::filesystem::current_path().string();

  std::error_code ec;
  const bool exists = std::filesystem::exists(settings_path, ec) && !ec;
  if (!exists && create_if_missing) {
    WriteDefaultSettingsToml(settings_path, result.issues);
  }

  ChatConfigFieldSet fields =
      LoadSettingsFromToml(settings_path, config, result.issues);
  if (fields.provider_id) {
    ApplyProviderDefaults(config, fields);
  }

  ApplyEnvOverrides(config, fields, result.issues);
  ResolveApiKey(config, fields, result.issues);

  return result;
}

}  // namespace yac::chat
