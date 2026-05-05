#include "chat/config.hpp"

#include "chat/config_paths.hpp"
#include "chat/settings_toml.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <filesystem>
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

double ParseTemperature(const std::string& value) {
  const double temp = std::stod(value);
  if (temp < kMinTemperature || temp > kMaxTemperature) {
    throw std::out_of_range("YAC_TEMPERATURE must be between 0.0 and 2.0");
  }
  return temp;
}

int ParseToolRoundLimit(const std::string& value) {
  size_t consumed = 0;
  const int rounds = std::stoi(value, &consumed);
  if (consumed != value.size()) {
    throw std::invalid_argument("YAC_MAX_TOOL_ROUNDS must be an integer");
  }
  if (rounds < kMinToolRoundLimit || rounds > kMaxToolRoundLimit) {
    throw std::out_of_range("YAC_MAX_TOOL_ROUNDS must be between " +
                            std::to_string(kMinToolRoundLimit) + " and " +
                            std::to_string(kMaxToolRoundLimit));
  }
  return rounds;
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
  auto it = presets.find(config.provider_id);
  if (it == presets.end()) {
    return;
  }
  if (!explicit_fields.model) {
    config.model = it->second.model;
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
    config.provider_id = std::move(*val);
    // Changing provider_id invalidates preset-derived fields unless the TOML
    // (or a prior env var) already fixed them.
    fields.provider_id = true;
    ApplyProviderDefaults(config, fields);
  }
  if (auto val = GetEnv("YAC_MODEL")) {
    config.model = std::move(*val);
    fields.model = true;
  }
  if (auto val = GetEnv("YAC_BASE_URL")) {
    config.base_url = std::move(*val);
    fields.base_url = true;
  }
  if (auto val = GetEnv("YAC_TEMPERATURE")) {
    try {
      config.temperature = ParseTemperature(*val);
      fields.temperature = true;
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_TEMPERATURE",
                        .detail = error.what()});
    }
  }
  if (auto val = GetEnv("YAC_MAX_TOOL_ROUNDS")) {
    try {
      config.max_tool_rounds = ParseToolRoundLimit(*val);
      fields.max_tool_rounds = true;
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_MAX_TOOL_ROUNDS",
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
    std::string normalized = *val;
    std::ranges::transform(normalized, normalized.begin(),
                           [](unsigned char ch) { return std::tolower(ch); });
    config.sync_terminal_background =
        !(normalized == "0" || normalized == "false" || normalized == "no" ||
          normalized == "off");
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
    std::string normalized = *val;
    std::ranges::transform(normalized, normalized.begin(),
                           [](unsigned char ch) { return std::tolower(ch); });
    config.auto_compact_enabled =
        !(normalized == "0" || normalized == "false" || normalized == "no" ||
          normalized == "off");
  }
  if (auto val = GetEnv("YAC_COMPACT_THRESHOLD")) {
    try {
      const double parsed = std::stod(*val);
      if (parsed < kMinAutoCompactThreshold ||
          parsed > kMaxAutoCompactThreshold) {
        throw std::out_of_range("YAC_COMPACT_THRESHOLD must be between " +
                                std::to_string(kMinAutoCompactThreshold) +
                                " and " +
                                std::to_string(kMaxAutoCompactThreshold));
      }
      config.auto_compact_threshold = parsed;
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_COMPACT_THRESHOLD",
                        .detail = error.what()});
    }
  }
  if (auto val = GetEnv("YAC_COMPACT_KEEP_LAST")) {
    try {
      size_t consumed = 0;
      const int parsed = std::stoi(*val, &consumed);
      if (consumed != val->size()) {
        throw std::invalid_argument("YAC_COMPACT_KEEP_LAST must be an integer");
      }
      if (parsed < kMinAutoCompactKeepLast ||
          parsed > kMaxAutoCompactKeepLast) {
        throw std::out_of_range("YAC_COMPACT_KEEP_LAST must be between " +
                                std::to_string(kMinAutoCompactKeepLast) +
                                " and " +
                                std::to_string(kMaxAutoCompactKeepLast));
      }
      config.auto_compact_keep_last = parsed;
    } catch (const std::exception& error) {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_COMPACT_KEEP_LAST",
                        .detail = error.what()});
    }
  }
  if (auto val = GetEnv("YAC_COMPACT_MODE")) {
    if (*val == "summarize" || *val == "truncate") {
      config.auto_compact_mode = std::move(*val);
    } else {
      issues.push_back({.severity = ConfigIssueSeverity::Error,
                        .message = "Invalid YAC_COMPACT_MODE",
                        .detail = "Value must be 'summarize' or 'truncate'."});
    }
  }

  // Bedrock-specific env overrides
  if (config.provider_id == "bedrock") {
    if (!config.options.count("region")) {
      config.options["region"] = "us-east-1";
    }
    if (!config.options.count("max_tokens")) {
      config.options["max_tokens"] = "4096";
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
    if (auto val = GetEnv("YAC_BEDROCK_MAX_TOKENS")) {
      config.options["max_tokens"] = std::move(*val);
    }
  }

  for (auto& server : config.mcp.servers) {
    const std::string server_prefix =
        "YAC_MCP_" + UpperSnakeCase(server.id) + "_";

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
      std::string normalized = *val;
      std::ranges::transform(normalized, normalized.begin(),
                             [](unsigned char ch) { return std::tolower(ch); });
      server.enabled = !(normalized == "0" || normalized == "false" ||
                         normalized == "no" || normalized == "off");
    }
    if (auto val = GetEnv((server_prefix + "API_KEY_ENV").c_str())) {
      if (server.auth.has_value()) {
        if (auto* bearer = std::get_if<mcp::McpAuthBearer>(&*server.auth)) {
          bearer->api_key_env = std::move(*val);
        }
      }
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
  // Bedrock uses AWS credential chain, not API key env var
  if (config.api_key.empty() && config.provider_id != "bedrock") {
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
