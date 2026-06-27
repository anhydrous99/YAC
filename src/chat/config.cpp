#include "chat/config.hpp"

#include "chat/config_paths.hpp"
#include "chat/reasoning_effort.hpp"
#include "chat/settings_registry.hpp"
#include "chat/settings_toml.hpp"
#include "chat/settings_validation.hpp"
#include "chat/sqlite_state_store.hpp"
#include "chat/state_store.hpp"
#include "provider/reasoning_effort_capability.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
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

bool IsBuiltInDefault(ConfigValueSource source) {
  return source == ConfigValueSource::BuiltInDefault;
}

bool IsKnownProviderId(std::string_view provider_id) {
  return provider_id == "openai-compatible" || provider_id == "openai" ||
         provider_id == "zai" || provider_id == "bedrock";
}

const SettingMetadata& Metadata(std::string_view key) {
  return *FindSettingsRegistryRecord(key);
}

void ValidateEnvNumber(std::string_view key, std::string_view env_var,
                       double parsed) {
  const auto& validation = Metadata(key).validation;
  if (validation.type == SettingValidationType::NumberRange &&
      (!std::isfinite(parsed) || parsed < validation.min_number ||
       parsed > validation.max_number)) {
    throw std::out_of_range(std::string(env_var) + " " +
                            EnvValidationDetail(validation));
  }
}

void ValidateEnvInteger(std::string_view key, std::string_view env_var,
                        int64_t parsed) {
  const auto& validation = Metadata(key).validation;
  if (validation.type == SettingValidationType::IntegerRange &&
      (parsed < validation.min_integer || parsed > validation.max_integer)) {
    throw std::out_of_range(std::string(env_var) + " " +
                            EnvValidationDetail(validation));
  }
  if (validation.type == SettingValidationType::PositiveInteger &&
      parsed <= 0) {
    throw std::invalid_argument(std::string(env_var) + " " +
                                EnvValidationDetail(validation));
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
                              EnvValidationDetail(validation));
}

uintmax_t ParsePositiveUintmax(const std::string& value, std::string_view key,
                               std::string_view env_var) {
  if (value.empty() || !std::ranges::all_of(value, [](unsigned char ch) {
        return std::isdigit(ch) != 0;
      })) {
    throw std::invalid_argument(std::string(env_var) + " " +
                                EnvValidationDetail(Metadata(key).validation));
  }
  size_t consumed = 0;
  const auto parsed = std::stoull(value, &consumed);
  if (consumed != value.size()) {
    throw std::invalid_argument(std::string(env_var) + " " +
                                EnvValidationDetail(Metadata(key).validation));
  }
  if (parsed > std::numeric_limits<uintmax_t>::max()) {
    throw std::out_of_range(std::string(env_var) + " is too large");
  }
  if (parsed == 0) {
    throw std::invalid_argument(std::string(env_var) + " " +
                                EnvValidationDetail(Metadata(key).validation));
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

std::optional<AppStateEntry> LoadStateEntry(const StateStore& state_store,
                                            std::string_view key,
                                            std::vector<ConfigIssue>& issues) {
  try {
    return state_store.LoadAppState(key);
  } catch (const std::exception& error) {
    issues.push_back({.severity = ConfigIssueSeverity::Warning,
                      .message = "Could not load SQLite app state",
                      .detail = error.what()});
    return std::nullopt;
  }
}

std::optional<ProviderProfile> LoadStateProfile(
    const StateStore& state_store, std::string_view profile_id,
    std::vector<ConfigIssue>& issues) {
  try {
    return state_store.LoadProviderProfile(profile_id);
  } catch (const std::exception& error) {
    issues.push_back({.severity = ConfigIssueSeverity::Warning,
                      .message = "Could not load SQLite provider profile",
                      .detail = error.what()});
    return std::nullopt;
  }
}

std::optional<ProviderProfile> EnabledProfile(
    std::optional<ProviderProfile> profile) {
  if (!profile.has_value() || !profile->enabled) {
    return std::nullopt;
  }
  return profile;
}

bool IsValidStateProfile(const ProviderProfile& profile,
                         std::vector<ConfigIssue>& issues) {
  if (!IsKnownProviderId(profile.provider_id.value)) {
    issues.push_back({.severity = ConfigIssueSeverity::Warning,
                      .message = "Ignoring invalid SQLite provider profile",
                      .detail = "Unknown provider id in stored profile '" +
                                profile.profile_id + "'."});
    return false;
  }
  std::vector<ConfigIssue> parse_issues;
  if (!ParseStringMapJson("SQLite provider profile options",
                          profile.options_json, parse_issues)
           .has_value()) {
    issues.push_back({.severity = ConfigIssueSeverity::Warning,
                      .message = "Ignoring invalid SQLite provider profile",
                      .detail = "Stored profile '" + profile.profile_id +
                                "' has invalid options_json."});
    return false;
  }
  return true;
}

std::optional<ProviderProfile> ResolveStateProfile(
    const StateStore& state_store, const ChatConfig& config,
    const std::optional<AppStateEntry>& profile_entry,
    std::vector<ConfigIssue>& issues) {
  if (profile_entry.has_value() && !profile_entry->value.empty()) {
    auto profile = EnabledProfile(
        LoadStateProfile(state_store, profile_entry->value, issues));
    if (profile.has_value() && !IsValidStateProfile(*profile, issues)) {
      return std::nullopt;
    }
    return profile;
  }
  auto profile = EnabledProfile(
      LoadStateProfile(state_store, config.provider_id.value, issues));
  if (profile.has_value() && !IsValidStateProfile(*profile, issues)) {
    return std::nullopt;
  }
  return profile;
}

void ApplyStateProviderOptions(ChatConfig& config,
                               const ProviderProfile& profile,
                               std::vector<ConfigIssue>& issues) {
  auto parsed_options = ParseStringMapJson("SQLite provider profile options",
                                           profile.options_json, issues);
  if (!parsed_options.has_value()) {
    return;
  }
  for (auto& [key, value] : *parsed_options) {
    const std::string option_key = key;
    const auto source = config.source.provider_options.find(key);
    if (source != config.source.provider_options.end() &&
        !IsBuiltInDefault(source->second)) {
      continue;
    }
    config.options[std::move(key)] = std::move(value);
    config.source.provider_options[option_key] = ConfigValueSource::StateStore;
  }
}

void ApplyStateModelEffort(ChatConfig& config, const StateStore& state_store,
                           const ChatConfigFieldSet& explicit_fields,
                           std::vector<ConfigIssue>& issues) {
  if (explicit_fields.model_settings) {
    return;
  }
  const auto key = LastModelEffortAppStateKey(config.provider_id, config.model);
  auto effort_entry = LoadStateEntry(state_store, key, issues);
  if (!effort_entry.has_value()) {
    return;
  }
  auto effort = ParseReasoningEffortValue(effort_entry->value);
  if (!effort.has_value()) {
    return;
  }
  config.model_settings.push_back(
      ProviderModelSettings{.provider_id = config.provider_id,
                            .model = config.model,
                            .effort = effort,
                            .source = ConfigValueSource::StateStore});
  config.source.model_settings.push_back(ConfigValueSource::StateStore);
}

void ApplyStateStoreOverrides(ChatConfig& config,
                              const ChatConfigFieldSet& explicit_fields,
                              const StateStore* state_store,
                              std::vector<ConfigIssue>& issues) {
  if (state_store == nullptr) {
    return;
  }

  const auto profile_entry =
      LoadStateEntry(*state_store, kAppStateLastProfileId, issues);
  const auto provider_entry =
      LoadStateEntry(*state_store, kAppStateLastProviderId, issues);
  std::optional<ProviderProfile> profile;
  if (profile_entry.has_value() && !profile_entry->value.empty()) {
    profile = ResolveStateProfile(*state_store, config, profile_entry, issues);
  }

  bool has_valid_state_provider_context =
      !IsBuiltInDefault(config.source.provider_id);
  if (IsBuiltInDefault(config.source.provider_id)) {
    if (profile.has_value() && IsKnownProviderId(profile->provider_id.value)) {
      config.provider_id = profile->provider_id;
      config.source.provider_id = ConfigValueSource::StateStore;
      has_valid_state_provider_context = true;
    } else if (provider_entry.has_value() &&
               IsKnownProviderId(provider_entry->value)) {
      config.provider_id = ProviderId{provider_entry->value};
      config.source.provider_id = ConfigValueSource::StateStore;
      has_valid_state_provider_context = true;
    }
  }

  if (config.source.provider_id == ConfigValueSource::StateStore) {
    ApplyProviderDefaults(config, explicit_fields);
  }

  if (!profile_entry.has_value() || profile_entry->value.empty()) {
    profile = ResolveStateProfile(*state_store, config, profile_entry, issues);
  }

  if (profile.has_value() && profile->provider_id == config.provider_id) {
    has_valid_state_provider_context = true;
    if (IsBuiltInDefault(config.source.profile_id)) {
      config.profile_id = profile->profile_id;
      config.source.profile_id = ConfigValueSource::StateStore;
    }
    if (IsBuiltInDefault(config.source.base_url) &&
        profile->base_url.has_value()) {
      config.base_url = *profile->base_url;
      config.source.base_url = ConfigValueSource::StateStore;
    }
    if (IsBuiltInDefault(config.source.model) &&
        profile->default_model.has_value()) {
      config.model = *profile->default_model;
      config.source.model = ConfigValueSource::StateStore;
    }
    ApplyStateProviderOptions(config, *profile, issues);
  }

  const bool has_stale_state_provider_context =
      !has_valid_state_provider_context &&
      ((profile_entry.has_value() && !profile_entry->value.empty()) ||
       (provider_entry.has_value() && !provider_entry->value.empty()));
  if ((IsBuiltInDefault(config.source.model) ||
       config.source.model == ConfigValueSource::StateStore) &&
      !has_stale_state_provider_context) {
    if (const auto model_entry =
            LoadStateEntry(*state_store, kAppStateLastModel, issues);
        model_entry.has_value() && !model_entry->value.empty()) {
      config.model = ModelId{model_entry->value};
      config.source.model = ConfigValueSource::StateStore;
    }
  }

  ApplyStateModelEffort(config, *state_store, explicit_fields, issues);
}

void ApplyEnvOverrides(ChatConfig& config, ChatConfigFieldSet& fields,
                       std::vector<ConfigIssue>& issues) {
  if (auto val = GetEnv("YAC_PROVIDER")) {
    config.provider_id = ProviderId{std::move(*val)};
    // Changing provider_id invalidates preset-derived fields unless the TOML
    // (or a prior env var) already fixed them.
    fields.provider_id = true;
    config.source.provider_id = ConfigValueSource::Environment;
    ApplyProviderDefaults(config, fields);
  }
  if (auto val = GetEnv("YAC_MODEL")) {
    config.model = ModelId{std::move(*val)};
    fields.model = true;
    config.source.model = ConfigValueSource::Environment;
  }
  if (auto val = GetEnv("YAC_BASE_URL")) {
    config.base_url = std::move(*val);
    fields.base_url = true;
    config.source.base_url = ConfigValueSource::Environment;
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
    config.source.api_key_env = ConfigValueSource::Environment;
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

  if (auto val = GetEnv("YAC_WEB_SEARCH_ENABLED")) {
    config.web_search.enabled = ParseEnvBool(*val);
  }
  if (auto val = GetEnv("YAC_WEB_SEARCH_PROVIDER")) {
    try {
      config.web_search.provider = ParseEnvStringEnum(
          *val, "web_search.provider", "YAC_WEB_SEARCH_PROVIDER");
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_WEB_SEARCH_PROVIDER",
                        .detail = error.what()});
    }
  }
  if (auto val = GetEnv("YAC_EXA_ENDPOINT")) {
    config.web_search.endpoint = std::move(*val);
  }
  if (auto val = GetEnv("YAC_WEB_SEARCH_TIMEOUT_SECONDS")) {
    try {
      config.web_search.timeout_seconds = ParseEnvInteger(
          *val, "web_search.timeout_seconds", "YAC_WEB_SEARCH_TIMEOUT_SECONDS");
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_WEB_SEARCH_TIMEOUT_SECONDS",
                        .detail = error.what()});
    }
  }

  // Bedrock-specific env overrides
  if (config.provider_id.value == "bedrock") {
    if (!config.options.contains("region")) {
      config.options["region"] = "us-east-1";
      config.source.provider_options["region"] =
          ConfigValueSource::BuiltInDefault;
    }
    if (!config.options.contains("max_tokens")) {
      config.options["max_tokens"] =
          std::to_string(Metadata("provider.options.max_tokens")
                             .runtime_default.integer_value);
      config.source.provider_options["max_tokens"] =
          ConfigValueSource::BuiltInDefault;
    }
    if (auto val = GetEnv("YAC_BEDROCK_REGION")) {
      config.options["region"] = std::move(*val);
      config.source.provider_options["region"] = ConfigValueSource::Environment;
    } else if (auto val = GetEnv("AWS_REGION")) {
      config.options["region"] = std::move(*val);
      config.source.provider_options["region"] = ConfigValueSource::Environment;
    }
    if (auto val = GetEnv("YAC_BEDROCK_PROFILE")) {
      config.options["profile"] = std::move(*val);
      config.source.provider_options["profile"] =
          ConfigValueSource::Environment;
    }
    if (auto val = GetEnv("YAC_BEDROCK_ENDPOINT_OVERRIDE")) {
      config.options["endpoint_override"] = std::move(*val);
      config.source.provider_options["endpoint_override"] =
          ConfigValueSource::Environment;
    }
    if (auto val = GetEnv("YAC_BEDROCK_CREDENTIAL_REFRESH_COMMAND")) {
      config.options["credential_refresh_command"] = std::move(*val);
      config.source.provider_options["credential_refresh_command"] =
          ConfigValueSource::Environment;
    }
    if (auto val = GetEnv("YAC_BEDROCK_MAX_TOKENS")) {
      config.options["max_tokens"] = std::move(*val);
      config.source.provider_options["max_tokens"] =
          ConfigValueSource::Environment;
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
      config.source.provider_options["max_tokens"] =
          ConfigValueSource::BuiltInDefault;
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

  // Mirror LoadOneMcpServer's validation so env overrides that produce an
  // unsupported transport or a half-configured server are dropped the same
  // way the TOML loader would reject them.
  std::erase_if(config.mcp.servers, [&issues](const auto& server) {
    if (server.transport != "stdio" && server.transport != "http") {
      issues.push_back(
          {.severity = ConfigIssueSeverity::Error,
           .message = "Invalid transport for mcp server '" + server.id + "'",
           .detail = "Must be 'stdio' or 'http'."});
      return true;
    }
    if (server.transport == "stdio" && server.command.empty()) {
      issues.push_back(
          {.severity = ConfigIssueSeverity::Error,
           .message = "mcp server '" + server.id + "' requires command",
           .detail = "transport='stdio' servers must specify command."});
      return true;
    }
    if (server.transport == "http" && server.url.empty()) {
      issues.push_back(
          {.severity = ConfigIssueSeverity::Error,
           .message = "mcp server '" + server.id + "' requires url",
           .detail = "transport='http' servers must specify url."});
      return true;
    }
    return false;
  });
}

void ResolveApiKey(ChatConfig& config, const ChatConfigFieldSet& fields,
                   std::vector<ConfigIssue>& issues) {
  // If settings.toml supplied an api_key directly, keep it. Otherwise fall
  // back to reading the env var named by api_key_env.
  if (!fields.api_key || config.api_key.empty()) {
    if (auto val = GetEnv(config.api_key_env.c_str())) {
      config.api_key = std::move(*val);
      config.source.api_key = ConfigValueSource::Environment;
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

void ResolveWebSearchConfig(ChatConfig& config,
                            std::vector<ConfigIssue>& issues) {
  if (config.web_search.provider != "exa") {
    issues.push_back({.severity = ConfigIssueSeverity::Error,
                      .message = "Invalid web_search.provider",
                      .detail = "Only 'exa' is supported for web_search."});
    return;
  }

  if (auto val = GetEnv(config.web_search.api_key_env.c_str())) {
    config.web_search.api_key = std::move(*val);
  }

  if (config.web_search.enabled && config.web_search.api_key.empty()) {
    issues.push_back({.severity = ConfigIssueSeverity::Error,
                      .message = "web_search provider is not configured",
                      .detail = "Set YAC_EXA_API_KEY before enabling "
                                "web_search."});
  }
}

void ValidateReasoningEffortOptionConflicts(const ChatConfig& config,
                                            std::vector<ConfigIssue>& issues) {
  for (const char* const key : {"reasoning_effort", "reasoning"}) {
    if (!config.options.contains(key)) {
      continue;
    }
    issues.push_back({.severity = ConfigIssueSeverity::Error,
                      .message = "provider.options." + std::string(key) +
                                 " conflicts with model-scoped effort"});
  }
}

ProviderConfig ActiveProviderConfig(const ChatConfig& config) {
  return ProviderConfig{.id = config.provider_id,
                        .profile_id = config.profile_id,
                        .model = config.model,
                        .api_key = config.api_key,
                        .api_key_env = config.api_key_env,
                        .base_url = config.base_url,
                        .system_prompt = config.system_prompt,
                        .options = config.options,
                        .context_window = config.context_window,
                        .state_store = config.state_store};
}

void ValidateActiveReasoningEffort(const ChatConfig& config,
                                   std::vector<ConfigIssue>& issues) {
  const auto settings_it =
      std::ranges::find_if(config.model_settings, [&](const auto& settings) {
        return settings.provider_id == config.provider_id &&
               settings.model == config.model;
      });
  if (settings_it == config.model_settings.end() ||
      !settings_it->effort.has_value()) {
    return;
  }

  const auto effort = *settings_it->effort;
  const auto capability = provider::LookupReasoningEffortCapability(
      ActiveProviderConfig(config), config.model.value);
  if (capability.has_value() &&
      std::ranges::find(capability->allowed_values, effort) !=
          capability->allowed_values.end()) {
    return;
  }

  issues.push_back(
      {.severity = ConfigIssueSeverity::Warning,
       .message = "Unsupported provider.model_settings.effort for active "
                  "provider model",
       .detail = "Effort '" + std::string(ReasoningEffortToString(effort)) +
                 "' is configured for " + config.provider_id.value + "/" +
                 config.model.value +
                 " but is not supported and will be "
                 "omitted from requests."});
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
    ValidateReasoningEffortOptionConflicts(config, result.issues);
    ValidateActiveReasoningEffort(config, result.issues);
    ResolveApiKey(config, fields, result.issues);
    ResolveWebSearchConfig(config, result.issues);
    return result;
  }
  try {
    auto state_store = std::make_shared<SQLiteStateStore>();
    auto result =
        LoadChatConfigResultFrom(settings_path,
                                 /*create_if_missing=*/true, state_store.get());
    result.config.state_store = std::move(state_store);
    return result;
  } catch (const std::exception& error) {
    auto result = LoadChatConfigResultFrom(settings_path,
                                           /*create_if_missing=*/true, nullptr);
    result.issues.push_back({.severity = ConfigIssueSeverity::Warning,
                             .message = "Could not open SQLite state store",
                             .detail = error.what()});
    return result;
  }
}

ChatConfigResult LoadChatConfigResultFrom(
    const std::filesystem::path& settings_path, bool create_if_missing) {
  return LoadChatConfigResultFrom(settings_path, create_if_missing, nullptr);
}

ChatConfigResult LoadChatConfigResultFrom(
    const std::filesystem::path& settings_path, bool create_if_missing,
    const StateStore* state_store) {
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
  ApplyStateStoreOverrides(config, fields, state_store, result.issues);
  ValidateReasoningEffortOptionConflicts(config, result.issues);
  ValidateActiveReasoningEffort(config, result.issues);
  ResolveApiKey(config, fields, result.issues);
  ResolveWebSearchConfig(config, result.issues);

  return result;
}

}  // namespace yac::chat
