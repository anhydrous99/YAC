#include "chat/settings_toml.hpp"

#include "chat/reasoning_effort.hpp"
#include "chat/settings_registry.hpp"
#include "chat/settings_toml_editor.hpp"
#include "chat/settings_toml_template.hpp"
#include "chat/settings_validation.hpp"
#include "mcp/mcp_server_config.hpp"

#include <cmath>
#include <exception>
#include <fstream>
#include <map>
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

const SettingMetadata& Metadata(std::string_view key) {
  return *FindSettingsRegistryRecord(key);
}

void ApplyRegistryRuntimeDefaults(ChatConfig& config) {
  const auto& temperature = Metadata("temperature").runtime_default;
  config.temperature = temperature.number_value;
  const auto& context_window =
      Metadata("provider.context_window").runtime_default;
  config.context_window = context_window.integer_value;
  const auto& sync_terminal_background =
      Metadata("theme.sync_terminal_background").runtime_default;
  config.sync_terminal_background = sync_terminal_background.bool_value;
  const auto& compact_threshold = Metadata("compact.threshold").runtime_default;
  config.auto_compact_threshold = compact_threshold.number_value;
  const auto& compact_keep_last = Metadata("compact.keep_last").runtime_default;
  config.auto_compact_keep_last = compact_keep_last.integer_value;
  const auto& compact_mode = Metadata("compact.mode").runtime_default;
  config.auto_compact_mode = compact_mode.string_value;
  const auto& result_max_bytes =
      Metadata("mcp.result_max_bytes").runtime_default;
  config.mcp.result_max_bytes =
      static_cast<uintmax_t>(result_max_bytes.integer_value);
  const auto& web_search_enabled =
      Metadata("web_search.enabled").runtime_default;
  config.web_search.enabled = web_search_enabled.bool_value;
  const auto& web_search_provider =
      Metadata("web_search.provider").runtime_default;
  config.web_search.provider = web_search_provider.string_value;
  const auto& web_search_endpoint =
      Metadata("web_search.endpoint").runtime_default;
  config.web_search.endpoint = web_search_endpoint.string_value;
  const auto& web_search_timeout =
      Metadata("web_search.timeout_seconds").runtime_default;
  config.web_search.timeout_seconds = web_search_timeout.integer_value;
  const auto& web_search_result_limit =
      Metadata("web_search.result_limit").runtime_default;
  config.web_search.result_limit = web_search_result_limit.integer_value;
  const auto& web_search_context_limit =
      Metadata("web_search.context_limit").runtime_default;
  config.web_search.context_limit = web_search_context_limit.integer_value;
}

bool ValidateNumberSetting(std::string_view key, double parsed,
                           std::vector<ConfigIssue>& issues) {
  const auto& validation = Metadata(key).validation;
  if (validation.type == SettingValidationType::NumberRange &&
      (!std::isfinite(parsed) || parsed < validation.min_number ||
       parsed > validation.max_number)) {
    AddError(issues, "Invalid " + std::string(key) + " in settings.toml",
             TomlValidationDetail(validation));
    return false;
  }
  return true;
}

bool ValidateIntegerSetting(std::string_view key, int64_t parsed,
                            std::vector<ConfigIssue>& issues) {
  const auto& validation = Metadata(key).validation;
  if (validation.type == SettingValidationType::IntegerRange &&
      (parsed < validation.min_integer || parsed > validation.max_integer)) {
    AddError(issues, "Invalid " + std::string(key) + " in settings.toml",
             TomlValidationDetail(validation));
    return false;
  }
  if (validation.type == SettingValidationType::PositiveInteger &&
      parsed <= 0) {
    AddError(issues, "Invalid " + std::string(key) + " in settings.toml",
             TomlValidationDetail(validation));
    return false;
  }
  return true;
}

bool ValidateStringEnumSetting(std::string_view key, std::string_view parsed,
                               std::vector<ConfigIssue>& issues) {
  const auto& validation = Metadata(key).validation;
  if (validation.type != SettingValidationType::StringEnum) {
    return true;
  }
  for (const auto allowed : validation.allowed_values) {
    if (parsed == allowed) {
      return true;
    }
  }
  AddError(issues, "Invalid " + std::string(key) + " in settings.toml",
           TomlValidationDetail(validation));
  return false;
}

bool ApplyNumberSetting(const toml::node_view<toml::node>& node,
                        std::string_view key, double& target,
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
    AddError(issues,
             "Invalid type for " + std::string(key) + " in settings.toml",
             "Expected a number.");
    return false;
  }
  if (!ValidateNumberSetting(key, parsed, issues)) {
    return false;
  }
  target = parsed;
  return true;
}

bool ApplyIntegerSetting(const toml::node_view<toml::node>& node,
                         std::string_view key, int& target,
                         std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  auto* value = node.as_integer();
  if (value == nullptr) {
    AddError(issues,
             "Invalid type for " + std::string(key) + " in settings.toml",
             "Expected an integer.");
    return false;
  }
  const auto parsed = value->get();
  if (!ValidateIntegerSetting(key, parsed, issues)) {
    return false;
  }
  target = static_cast<int>(parsed);
  return true;
}

bool ApplyPositiveUintmaxSetting(const toml::node_view<toml::node>& node,
                                 std::string_view key, uintmax_t& target,
                                 std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  auto* value = node.as_integer();
  if (value == nullptr) {
    AddError(issues,
             "Invalid type for " + std::string(key) + " in settings.toml",
             "Expected an integer.");
    return false;
  }
  const auto parsed = value->get();
  if (!ValidateIntegerSetting(key, parsed, issues)) {
    return false;
  }
  target = static_cast<uintmax_t>(parsed);
  return true;
}

bool ApplyStringEnumSetting(const toml::node_view<toml::node>& node,
                            std::string_view key, std::string& target,
                            std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  auto* value = node.as_string();
  if (value == nullptr) {
    AddError(issues,
             "Invalid type for " + std::string(key) + " in settings.toml",
             "Expected a string.");
    return false;
  }
  const std::string parsed = value->get();
  if (!ValidateStringEnumSetting(key, parsed, issues)) {
    return false;
  }
  target = parsed;
  return true;
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

bool ApplyProviderOptionString(const toml::node_view<toml::node>& node,
                               const std::string& key,
                               std::map<std::string, std::string>& options,
                               std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  if (auto* value = node.as_string()) {
    options[key] = value->get();
    return true;
  }
  AddError(issues,
           "Invalid type for provider.options." + key + " in settings.toml",
           "Expected a string.");
  return false;
}

bool ApplyProviderOptionIntegerString(
    const toml::node_view<toml::node>& node, const std::string& key,
    std::map<std::string, std::string>& options,
    std::vector<ConfigIssue>& issues) {
  if (!node) {
    return false;
  }
  if (auto* value = node.as_string()) {
    options[key] = value->get();
    return true;
  }
  if (auto* value = node.as_integer()) {
    options[key] = std::to_string(value->get());
    return true;
  }
  AddError(issues,
           "Invalid type for provider.options." + key + " in settings.toml",
           "Expected a string or integer.");
  return false;
}

void LoadBedrockProviderOptions(const toml::node_view<toml::node>& provider,
                                ChatConfig& config,
                                std::vector<ConfigIssue>& issues) {
  if (config.provider_id.value != "bedrock") {
    return;
  }

  const auto options = provider["options"];
  if (!options) {
    return;
  }
  auto* options_table = options.as_table();
  if (options_table == nullptr) {
    AddError(issues, "Invalid type for [provider.options] in settings.toml",
             "Expected a table.");
    return;
  }

  if (ApplyProviderOptionString((*options_table)["region"], "region",
                                config.options, issues)) {
    config.source.provider_options["region"] = ConfigValueSource::Toml;
  }
  if (ApplyProviderOptionIntegerString((*options_table)["max_tokens"],
                                       "max_tokens", config.options, issues)) {
    config.source.provider_options["max_tokens"] = ConfigValueSource::Toml;
  }
  if (ApplyProviderOptionString((*options_table)["profile"], "profile",
                                config.options, issues)) {
    config.source.provider_options["profile"] = ConfigValueSource::Toml;
  }
  if (ApplyProviderOptionString((*options_table)["endpoint_override"],
                                "endpoint_override", config.options, issues)) {
    config.source.provider_options["endpoint_override"] =
        ConfigValueSource::Toml;
  }
  if (ApplyProviderOptionString((*options_table)["credential_refresh_command"],
                                "credential_refresh_command", config.options,
                                issues)) {
    config.source.provider_options["credential_refresh_command"] =
        ConfigValueSource::Toml;
  }
}

bool ApplyTemperature(const toml::node_view<toml::node>& node, double& target,
                      std::vector<ConfigIssue>& issues) {
  return ApplyNumberSetting(node, "temperature", target, issues);
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
  const auto provider_section = table["provider"];
  if (provider_section && !provider_section.is_table()) {
    AddError(issues, "Invalid type for [provider] in settings.toml",
             "Expected a table.");
    return false;
  }
  const auto model_settings = table["provider"]["model_settings"];
  if (model_settings && !model_settings.is_array()) {
    AddError(issues,
             "Invalid type for [[provider.model_settings]] in settings.toml",
             "Expected an array of tables.");
    return false;
  }
  if (auto* settings_array = model_settings.as_array()) {
    for (const auto& entry : *settings_array) {
      if (!entry.is_table()) {
        AddError(issues, "Invalid element in [[provider.model_settings]]",
                 "Expected a table.");
        return false;
      }
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

void LoadProviderSection(toml::table& root, ChatConfig& config,
                         ChatConfigFieldSet& fields,
                         std::vector<ConfigIssue>& issues) {
  const auto provider = root["provider"];
  if (!provider.is_table()) {
    if (root.contains("provider")) {
      AddError(issues, "Invalid type for [provider] in settings.toml",
               "Expected a table.");
    }
    return;
  }
  fields.provider_id = ApplyStringField(provider["id"], "provider.id",
                                        config.provider_id.value, issues);
  fields.model = ApplyStringField(provider["model"], "provider.model",
                                  config.model.value, issues);
  fields.base_url = ApplyStringField(provider["base_url"], "provider.base_url",
                                     config.base_url, issues);
  fields.api_key_env =
      ApplyStringField(provider["api_key_env"], "provider.api_key_env",
                       config.api_key_env, issues);
  fields.api_key = ApplyStringField(provider["api_key"], "provider.api_key",
                                    config.api_key, issues);
  if (fields.provider_id) {
    config.source.provider_id = ConfigValueSource::Toml;
  }
  if (fields.model) {
    config.source.model = ConfigValueSource::Toml;
  }
  if (fields.base_url) {
    config.source.base_url = ConfigValueSource::Toml;
  }
  if (fields.api_key_env) {
    config.source.api_key_env = ConfigValueSource::Toml;
  }
  if (fields.api_key) {
    config.source.api_key = ConfigValueSource::Toml;
  }
  fields.context_window =
      ApplyIntegerSetting(provider["context_window"], "provider.context_window",
                          config.context_window, issues);
  if (auto* options_table = provider["options"].as_table()) {
    for (const char* const key : {"reasoning_effort", "reasoning"}) {
      const auto option = (*options_table)[key];
      if (!option) {
        continue;
      }
      if (auto* value = option.as_string()) {
        config.options[key] = value->get();
        config.source.provider_options[key] = ConfigValueSource::Toml;
      } else {
        AddError(issues,
                 "Invalid type for provider.options." + std::string(key) +
                     " in settings.toml",
                 "Expected a string.");
      }
    }
  } else if (provider["options"]) {
    AddError(issues, "Invalid type for [provider.options] in settings.toml",
             "Expected a table.");
  }
  LoadBedrockProviderOptions(provider, config, issues);
}

void LoadProviderModelSettings(toml::table& root, ChatConfig& config,
                               ChatConfigFieldSet& fields,
                               std::vector<ConfigIssue>& issues) {
  const auto settings = root["provider"]["model_settings"];
  if (!settings) {
    return;
  }
  fields.model_settings = true;
  auto* settings_array = settings.as_array();
  if (settings_array == nullptr) {
    AddError(issues,
             "Invalid type for [[provider.model_settings]] in settings.toml",
             "Expected an array of tables.");
    return;
  }

  for (auto& entry : *settings_array) {
    auto* entry_table = entry.as_table();
    if (entry_table == nullptr) {
      AddError(issues, "Invalid element in [[provider.model_settings]]",
               "Expected a table.");
      continue;
    }

    ProviderModelSettings parsed;
    const bool has_provider = ApplyStringField(
        (*entry_table)["provider"], "provider.model_settings.provider",
        parsed.provider_id.value, issues);
    const bool has_model = ApplyStringField((*entry_table)["model"],
                                            "provider.model_settings.model",
                                            parsed.model.value, issues);
    if (!has_provider || !has_model) {
      AddError(issues, "Missing provider.model_settings provider or model",
               "Each entry requires string provider and model fields.");
      continue;
    }
    if (auto effort_node = (*entry_table)["effort"]) {
      if (auto* effort_string = effort_node.as_string()) {
        const std::string raw_effort = effort_string->get();
        if (auto effort = ParseReasoningEffortValue(raw_effort)) {
          parsed.effort = effort;
        } else {
          AddError(issues,
                   "Invalid provider.model_settings.effort in settings.toml",
                   "Unsupported effort '" + raw_effort + "'.");
        }
      } else {
        AddError(issues,
                 "Invalid type for provider.model_settings.effort in "
                 "settings.toml",
                 "Expected a string.");
      }
    }
    parsed.source = ConfigValueSource::Toml;
    config.model_settings.push_back(std::move(parsed));
    config.source.model_settings.push_back(ConfigValueSource::Toml);
  }
}

void LoadLspSection(toml::table& root, ChatConfig& config,
                    ChatConfigFieldSet& fields,
                    std::vector<ConfigIssue>& issues) {
  const auto clangd = root["lsp"]["clangd"];
  if (clangd.is_table()) {
    fields.lsp_clangd_command =
        ApplyStringField(clangd["command"], "lsp.clangd.command",
                         config.lsp_clangd_command, issues);
    fields.lsp_clangd_args = ApplyStringArray(clangd["args"], "lsp.clangd.args",
                                              config.lsp_clangd_args, issues);
    return;
  }
  if (root.contains("lsp") && !root["lsp"].is_table()) {
    AddError(issues, "Invalid type for [lsp] in settings.toml",
             "Expected a table.");
  }
}

void LoadThemeSection(toml::table& root, ChatConfig& config,
                      ChatConfigFieldSet& fields,
                      std::vector<ConfigIssue>& issues) {
  const auto theme_section = root["theme"];
  if (!theme_section.is_table()) {
    if (root.contains("theme")) {
      AddError(issues, "Invalid type for [theme] in settings.toml",
               "Expected a table.");
    }
    return;
  }
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
}

void LoadCompactSection(toml::table& root, ChatConfig& config,
                        std::vector<ConfigIssue>& issues) {
  const auto compact_section = root["compact"];
  if (!compact_section.is_table()) {
    if (root.contains("compact")) {
      AddError(issues, "Invalid type for [compact] in settings.toml",
               "Expected a table.");
    }
    return;
  }
  ApplyBoolField(compact_section["auto_enabled"], "compact.auto_enabled",
                 config.auto_compact_enabled, issues);
  ApplyNumberSetting(compact_section["threshold"], "compact.threshold",
                     config.auto_compact_threshold, issues);
  ApplyIntegerSetting(compact_section["keep_last"], "compact.keep_last",
                      config.auto_compact_keep_last, issues);
  ApplyStringEnumSetting(compact_section["mode"], "compact.mode",
                         config.auto_compact_mode, issues);
}

void LoadWebSearchSection(toml::table& root, ChatConfig& config,
                          std::vector<ConfigIssue>& issues) {
  const auto web_search_section = root["web_search"];
  if (!web_search_section.is_table()) {
    if (root.contains("web_search")) {
      AddError(issues, "Invalid type for [web_search] in settings.toml",
               "Expected a table.");
    }
    return;
  }

  ApplyBoolField(web_search_section["enabled"], "web_search.enabled",
                 config.web_search.enabled, issues);
  ApplyStringEnumSetting(web_search_section["provider"], "web_search.provider",
                         config.web_search.provider, issues);
  ApplyStringField(web_search_section["endpoint"], "web_search.endpoint",
                   config.web_search.endpoint, issues);
  ApplyIntegerSetting(web_search_section["timeout_seconds"],
                      "web_search.timeout_seconds",
                      config.web_search.timeout_seconds, issues);
  ApplyIntegerSetting(web_search_section["result_limit"],
                      "web_search.result_limit", config.web_search.result_limit,
                      issues);
  ApplyIntegerSetting(web_search_section["context_limit"],
                      "web_search.context_limit",
                      config.web_search.context_limit, issues);
}

// Reads the [auth] sub-table for an MCP server and returns the parsed auth
// variant, or std::nullopt when no valid auth is configured. Errors are
// recorded in `issues` and the bearer's api_key_env flag is mirrored into
// `field_set` so presets only fill unset fields.
std::optional<std::variant<mcp::McpAuthBearer, mcp::McpAuthOAuth>> ParseMcpAuth(
    toml::table& auth_tbl, std::string_view server_id,
    McpServerFieldSet& field_set, std::vector<ConfigIssue>& issues) {
  auto* type_v = auth_tbl["type"].as_string();
  if (type_v == nullptr) {
    AddError(
        issues,
        "mcp.servers.auth missing 'type' for '" + std::string(server_id) + "'",
        "Expected 'bearer' or 'oauth'.");
    return std::nullopt;
  }
  const std::string auth_type = type_v->get();
  if (auth_type == "bearer") {
    mcp::McpAuthBearer bearer;
    if (auto* v = auth_tbl["api_key_env"].as_string()) {
      bearer.api_key_env = v->get();
      field_set.api_key_env = true;
    }
    return std::variant<mcp::McpAuthBearer, mcp::McpAuthOAuth>{
        std::move(bearer)};
  }
  if (auth_type == "oauth") {
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
    ApplyStringArray(auth_tbl["scopes"], "mcp.servers.auth.scopes",
                     oauth.scopes, issues);
    return std::variant<mcp::McpAuthBearer, mcp::McpAuthOAuth>{
        std::move(oauth)};
  }
  AddError(issues, "Unknown mcp.servers.auth.type: " + auth_type,
           "Expected 'bearer' or 'oauth'.");
  return std::nullopt;
}

// Parses a single [[mcp.servers]] entry. Returns std::nullopt for entries
// rejected by validation (missing id, duplicate id, bad transport); the caller
// must skip such entries without recording them.
std::optional<std::pair<mcp::McpServerConfig, McpServerFieldSet>>
LoadOneMcpServer(toml::table& server_tbl,
                 std::unordered_set<std::string>& seen_ids,
                 std::vector<ConfigIssue>& issues) {
  mcp::McpServerConfig srv;
  McpServerFieldSet field_set;

  if (auto* v = server_tbl["id"].as_string()) {
    srv.id = v->get();
  }
  if (srv.id.empty()) {
    AddError(issues, "Missing or empty id in [[mcp.servers]]",
             "Each server entry must have a non-empty id.");
    return std::nullopt;
  }
  if (seen_ids.contains(srv.id)) {
    AddError(issues, "Duplicate mcp.servers id: " + srv.id,
             "duplicate server id; keeping first entry.");
    return std::nullopt;
  }
  seen_ids.insert(srv.id);

  if (auto* v = server_tbl["transport"].as_string()) {
    srv.transport = v->get();
  }
  if (auto* v = server_tbl["command"].as_string()) {
    srv.command = v->get();
    field_set.command = true;
  }
  ApplyStringArray(server_tbl["args"], "mcp.servers.args", srv.args, issues);
  if (server_tbl["args"]) {
    field_set.args = true;
  }
  if (auto* v = server_tbl["url"].as_string()) {
    srv.url = v->get();
    field_set.url = true;
  }

  if (auto* env_tbl = server_tbl["env"].as_table()) {
    for (auto& [k, v] : *env_tbl) {
      if (auto* sv = v.as_string()) {
        srv.env.emplace(std::string(k), sv->get());
      } else {
        AddError(issues, "Invalid value in mcp.servers.env",
                 "All env values must be strings.");
      }
    }
  } else if (server_tbl["env"]) {
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
    return std::nullopt;
  }
  if (srv.transport == "stdio" && srv.command.empty()) {
    AddError(issues, "mcp server '" + srv.id + "' requires command",
             "transport='stdio' servers must specify command.");
    return std::nullopt;
  }
  if (srv.transport == "http" && srv.url.empty()) {
    AddError(issues, "mcp server '" + srv.id + "' requires url",
             "transport='http' servers must specify url.");
    return std::nullopt;
  }

  if (auto* v = server_tbl["enabled"].as_boolean()) {
    srv.enabled = v->get();
    field_set.enabled = true;
  }
  if (auto* v = server_tbl["requires_approval"].as_boolean()) {
    srv.requires_approval = v->get();
  }
  if (auto* v = server_tbl["auto_start"].as_boolean()) {
    srv.auto_start = v->get();
  }
  ApplyStringArray(server_tbl["approval_required_tools"],
                   "mcp.servers.approval_required_tools",
                   srv.approval_required_tools, issues);

  if (auto* auth_tbl = server_tbl["auth"].as_table()) {
    if (auto auth = ParseMcpAuth(*auth_tbl, srv.id, field_set, issues)) {
      srv.auth = std::move(*auth);
    }
  } else if (server_tbl["auth"]) {
    AddError(issues, "Invalid type for mcp.servers.auth for '" + srv.id + "'",
             "Expected a table.");
  }

  if (auto* headers_tbl = server_tbl["headers"].as_table()) {
    for (auto& [k, v] : *headers_tbl) {
      if (auto* sv = v.as_string()) {
        srv.headers.emplace(std::string(k), sv->get());
      } else {
        AddError(issues,
                 "Invalid value in mcp.servers.headers for '" + srv.id + "'",
                 "All header values must be strings.");
      }
    }
  } else if (server_tbl["headers"]) {
    AddError(issues,
             "Invalid type for mcp.servers.headers for '" + srv.id + "'",
             "Expected a table of string values.");
  }

  return std::make_pair(std::move(srv), std::move(field_set));
}

void LoadMcpSection(toml::table& root, ChatConfig& config,
                    ChatConfigFieldSet& fields,
                    std::vector<ConfigIssue>& issues) {
  const auto mcp_section = root["mcp"];
  if (!mcp_section.is_table()) {
    if (root.contains("mcp")) {
      AddError(issues, "Invalid type for [mcp] in settings.toml",
               "Expected a table.");
    }
    return;
  }

  ApplyPositiveUintmaxSetting(mcp_section["result_max_bytes"],
                              "mcp.result_max_bytes",
                              config.mcp.result_max_bytes, issues);

  const auto servers_node = mcp_section["servers"];
  if (!servers_node.is_array()) {
    if (mcp_section["servers"]) {
      AddError(issues, "Invalid type for [[mcp.servers]] in settings.toml",
               "Expected an array of tables.");
    }
    return;
  }

  std::unordered_set<std::string> seen_ids;
  for (auto& server_elem : *servers_node.as_array()) {
    auto* server_tbl = server_elem.as_table();
    if (server_tbl == nullptr) {
      AddError(issues, "Invalid element in [[mcp.servers]]",
               "Expected a table.");
      continue;
    }
    auto loaded = LoadOneMcpServer(*server_tbl, seen_ids, issues);
    if (!loaded) {
      continue;
    }
    fields.mcp_servers.emplace(loaded->first.id, std::move(loaded->second));
    config.mcp.servers.push_back(std::move(loaded->first));
  }
}

}  // namespace

ChatConfigFieldSet LoadSettingsFromToml(const std::filesystem::path& path,
                                        ChatConfig& config,
                                        std::vector<ConfigIssue>& issues) {
  ApplyRegistryRuntimeDefaults(config);
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

  if (auto* system_prompt = table["system_prompt"].as_string()) {
    config.system_prompt = system_prompt->get();
    fields.system_prompt = true;
  } else if (table.contains("system_prompt")) {
    AddError(issues, "Invalid type for system_prompt in settings.toml",
             "Expected a string.");
  }

  fields.workspace_root = ApplyStringField(
      table["workspace_root"], "workspace_root", config.workspace_root, issues);

  LoadProviderSection(table, config, fields, issues);
  LoadProviderModelSettings(table, config, fields, issues);
  LoadLspSection(table, config, fields, issues);
  LoadThemeSection(table, config, fields, issues);
  LoadCompactSection(table, config, issues);
  LoadWebSearchSection(table, config, issues);
  LoadMcpSection(table, config, fields, issues);

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

bool SaveProviderModelEffortToSettingsToml(
    const std::filesystem::path& path, const ProviderId& provider_id,
    const ModelId& model, std::optional<ReasoningEffort> effort,
    std::vector<ConfigIssue>& issues) {
  if (provider_id.value.empty() || model.value.empty()) {
    AddError(issues, "Cannot save provider model effort",
             "Provider and model must both be non-empty.");
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

  std::string content;
  if (!ReadTextFile(path, content, issues)) {
    return false;
  }

  const auto updated = WithProviderModelEffort(
      content, provider_id, model, effort,
      MatchingModelSettingsIndex(table, provider_id, model), issues);
  if (updated == content) {
    return issues.empty();
  }
  if (!ValidateGeneratedSettingsToml(updated, path, issues)) {
    return false;
  }

  return WriteTextFile(path, updated, issues);
}

}  // namespace yac::chat
