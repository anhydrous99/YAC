#include "chat/config.hpp"

#include "chat/config_paths.hpp"
#include "chat/settings_toml.hpp"

#include <cstdlib>
#include <exception>
#include <filesystem>
#include <sstream>
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
  constexpr double kMinTemp = 0.0;
  constexpr double kMaxTemp = 2.0;
  const double temp = std::stod(value);
  if (temp < kMinTemp || temp > kMaxTemp) {
    throw std::out_of_range("YAC_TEMPERATURE must be between 0.0 and 2.0");
  }
  return temp;
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
  if (config.api_key.empty()) {
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
