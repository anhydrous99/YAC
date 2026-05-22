#include "chat/settings_registry.hpp"

#include <array>
#include <string>
#include <unordered_set>
#include <utility>

namespace yac::chat {

namespace {

using Class = SettingClassification;
using Docs = SettingsDocExpectation;
using Type = SettingValueType;

constexpr Docs kReadmeExampleTemplate{
    .readme = true, .settings_example = true, .default_template = true};
constexpr Docs kReadmeExample{.readme = true, .settings_example = true};
constexpr Docs kOpenAiAuthDocs{.readme = true,
                               .settings_example = true,
                               .default_template = true,
                               .openai_auth_docs = true};
constexpr Docs kMcpDocs{.readme = true,
                        .settings_example = true,
                        .default_template = true,
                        .mcp_docs = true};

constexpr std::array<SettingClassification, 6> kClassifications = {
    Class::UserFacing, Class::Secret,   Class::Operational,
    Class::External,   Class::Internal, Class::Deprecated};

constexpr std::array<SettingMetadata, 32> kRecords = {{
    {.key = "temperature",
     .toml_path = "temperature",
     .env_var = "YAC_TEMPERATURE",
     .value_type = Type::Number,
     .default_description = "0.7; valid range 0.0 to 2.0",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "system_prompt",
     .toml_path = "system_prompt",
     .env_var = "YAC_SYSTEM_PROMPT",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "workspace_root",
     .toml_path = "workspace_root",
     .env_var = "YAC_WORKSPACE_ROOT",
     .value_type = Type::String,
     .default_description = "launch current working directory",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "provider.id",
     .toml_path = "provider.id",
     .env_var = "YAC_PROVIDER",
     .value_type = Type::String,
     .default_description =
         "openai-compatible; provider presets may override dependent defaults",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "provider.model",
     .toml_path = "provider.model",
     .env_var = "YAC_MODEL",
     .value_type = Type::String,
     .default_description =
         "gpt-4o-mini; provider presets may supply provider-specific defaults",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "provider.base_url",
     .toml_path = "provider.base_url",
     .env_var = "YAC_BASE_URL",
     .value_type = Type::String,
     .default_description = "https://api.openai.com/v1/; provider presets may "
                            "supply provider-specific defaults",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "provider.api_key_env",
     .toml_path = "provider.api_key_env",
     .env_var = "YAC_API_KEY_ENV",
     .value_type = Type::String,
     .default_description = "OPENAI_API_KEY; provider presets may supply "
                            "provider-specific defaults",
     .classification = Class::UserFacing,
     .docs = kOpenAiAuthDocs},
    {.key = "provider.context_window",
     .toml_path = "provider.context_window",
     .env_var = "YAC_CONTEXT_WINDOW",
     .value_type = Type::Integer,
     .default_description =
         "0 means auto-detect; valid range 1 to 10000000 when set",
     .classification = Class::UserFacing,
     .docs = kReadmeExample},
    {.key = "lsp.clangd.command",
     .toml_path = "lsp.clangd.command",
     .env_var = "YAC_LSP_CLANGD_COMMAND",
     .value_type = Type::String,
     .default_description = "clangd",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "lsp.clangd.args",
     .toml_path = "lsp.clangd.args",
     .env_var = "YAC_LSP_CLANGD_ARGS",
     .value_type = Type::StringArray,
     .default_description = "empty array; env override is whitespace-split",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "theme.sync_terminal_background",
     .toml_path = "theme.sync_terminal_background",
     .env_var = "YAC_SYNC_TERMINAL_BACKGROUND",
     .value_type = Type::Bool,
     .default_description = "true",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "theme.name",
     .toml_path = "theme.name",
     .env_var = "YAC_THEME_NAME",
     .value_type = Type::String,
     .default_description = "vivid",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "theme.density",
     .toml_path = "theme.density",
     .env_var = "YAC_THEME_DENSITY",
     .value_type = Type::String,
     .default_description = "comfortable",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "compact.auto_enabled",
     .toml_path = "compact.auto_enabled",
     .env_var = "YAC_COMPACT_AUTO_ENABLED",
     .value_type = Type::Bool,
     .default_description = "true",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "compact.threshold",
     .toml_path = "compact.threshold",
     .env_var = "YAC_COMPACT_THRESHOLD",
     .value_type = Type::Number,
     .default_description = "0.8; valid range 0.05 to 1.0",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "compact.keep_last",
     .toml_path = "compact.keep_last",
     .env_var = "YAC_COMPACT_KEEP_LAST",
     .value_type = Type::Integer,
     .default_description = "20; valid range 1 to 10000",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "compact.mode",
     .toml_path = "compact.mode",
     .env_var = "YAC_COMPACT_MODE",
     .value_type = Type::String,
     .default_description = "summarize; valid values are summarize or truncate",
     .classification = Class::UserFacing,
     .docs = kReadmeExampleTemplate},
    {.key = "provider.api_key",
     .toml_path = "provider.api_key",
     .value_type = Type::Secret,
     .default_description = "unset; lowest-priority inline secret for openai",
     .classification = Class::Secret,
     .docs = kOpenAiAuthDocs},
    {.key = "openai.api_key_env_value",
     .env_var = "OPENAI_API_KEY",
     .value_type = Type::Secret,
     .default_description =
         "unset; used through provider.api_key_env and OpenAI auth precedence",
     .classification = Class::Secret,
     .docs = kOpenAiAuthDocs},
    {.key = "zai.api_key_env_value",
     .env_var = "ZAI_API_KEY",
     .value_type = Type::Secret,
     .default_description = "unset; provider preset default for zai",
     .classification = Class::Secret,
     .docs = kReadmeExampleTemplate},
    {.key = "mcp.result_max_bytes",
     .toml_path = "mcp.result_max_bytes",
     .env_var = "YAC_MCP_RESULT_MAX_BYTES",
     .value_type = Type::Integer,
     .default_description = "262144; env/TOML values must be positive integers",
     .classification = Class::UserFacing,
     .docs = kMcpDocs},
    {.key = "provider.options.region",
     .toml_path = "provider.options.region",
     .env_var = "YAC_BEDROCK_REGION",
     .value_type = Type::String,
     .default_description = "us-east-1 when provider.id is bedrock",
     .classification = Class::UserFacing,
     .docs = kReadmeExample},
    {.key = "bedrock.aws_region_fallback",
     .env_var = "AWS_REGION",
     .value_type = Type::String,
     .default_description = "fallback for provider.options.region when "
                            "YAC_BEDROCK_REGION is unset",
     .classification = Class::External,
     .docs = kReadmeExample},
    {.key = "process.home_dir_fallback",
     .env_var = "HOME",
     .value_type = Type::String,
     .default_description = "fallback for resolving config and auth paths",
     .classification = Class::External,
     .docs = {.readme = true}},
    {.key = "process.path_lookup",
     .env_var = "PATH",
     .value_type = Type::String,
     .default_description = "search path for executables and helper binaries",
     .classification = Class::External,
     .docs = {.readme = true}},
    {.key = "provider.options.profile",
     .toml_path = "provider.options.profile",
     .env_var = "YAC_BEDROCK_PROFILE",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::UserFacing,
     .docs = kReadmeExample},
    {.key = "provider.options.endpoint_override",
     .toml_path = "provider.options.endpoint_override",
     .env_var = "YAC_BEDROCK_ENDPOINT_OVERRIDE",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::UserFacing,
     .docs = kReadmeExample},
    {.key = "provider.options.credential_refresh_command",
     .toml_path = "provider.options.credential_refresh_command",
     .env_var = "YAC_BEDROCK_CREDENTIAL_REFRESH_COMMAND",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::UserFacing,
     .docs = kReadmeExample},
    {.key = "provider.options.max_tokens",
     .toml_path = "provider.options.max_tokens",
     .env_var = "YAC_BEDROCK_MAX_TOKENS",
     .value_type = Type::Integer,
     .default_description =
         "4096 when provider.id is bedrock; valid range 1 to 200000",
     .classification = Class::UserFacing,
     .docs = kReadmeExample},
    {.key = "openai.auth_store",
     .env_var = "YAC_OPENAI_AUTH_STORE",
     .value_type = Type::String,
     .default_description = "keychain-first; file disables keychain probing",
     .classification = Class::Operational,
     .docs = kOpenAiAuthDocs},
    {.key = "mcp.token_store",
     .env_var = "YAC_MCP_TOKEN_STORE",
     .value_type = Type::String,
     .default_description = "default token-store selection for MCP auth CLI",
     .classification = Class::Operational,
     .docs = {.mcp_docs = true}},
    {.key = "keychain.disabled",
     .env_var = "YAC_KEYCHAIN_DISABLED",
     .value_type = Type::Bool,
     .default_description = "unset; disables keychain token store when present",
     .classification = Class::Operational},
}};

constexpr std::array<DynamicSettingPattern, 17> kDynamicPatterns = {{
    {.key = "mcp.servers.id",
     .toml_path_pattern = "mcp.servers[].id",
     .value_type = Type::String,
     .default_description = "required unique server id",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.transport",
     .toml_path_pattern = "mcp.servers[].transport",
     .env_var_pattern = "YAC_MCP_<ID>_TRANSPORT",
     .value_type = Type::String,
     .default_description = "stdio or http",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.command",
     .toml_path_pattern = "mcp.servers[].command",
     .env_var_pattern = "YAC_MCP_<ID>_COMMAND",
     .value_type = Type::String,
     .default_description = "unset except preset-backed server IDs",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.args",
     .toml_path_pattern = "mcp.servers[].args",
     .env_var_pattern = "YAC_MCP_<ID>_ARGS",
     .value_type = Type::StringArray,
     .default_description = "empty array except preset-backed server IDs; env "
                            "override is whitespace-split",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.url",
     .toml_path_pattern = "mcp.servers[].url",
     .env_var_pattern = "YAC_MCP_<ID>_URL",
     .value_type = Type::String,
     .default_description = "unset except preset-backed server IDs",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.enabled",
     .toml_path_pattern = "mcp.servers[].enabled",
     .env_var_pattern = "YAC_MCP_<ID>_ENABLED",
     .value_type = Type::Bool,
     .default_description = "true",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.auto_start",
     .toml_path_pattern = "mcp.servers[].auto_start",
     .env_var_pattern = "YAC_MCP_<ID>_AUTO_START",
     .value_type = Type::Bool,
     .default_description = "true",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.requires_approval",
     .toml_path_pattern = "mcp.servers[].requires_approval",
     .env_var_pattern = "YAC_MCP_<ID>_REQUIRES_APPROVAL",
     .value_type = Type::Bool,
     .default_description = "false",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.approval_required_tools",
     .toml_path_pattern = "mcp.servers[].approval_required_tools",
     .env_var_pattern = "YAC_MCP_<ID>_APPROVAL_REQUIRED_TOOLS",
     .value_type = Type::StringArray,
     .default_description = "empty array; env override is whitespace-split",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.auth.api_key_env",
     .toml_path_pattern = "mcp.servers[].auth.api_key_env",
     .env_var_pattern = "YAC_MCP_<ID>_API_KEY_ENV",
     .value_type = Type::String,
     .default_description = "unset; names bearer-token env var only",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.env_json",
     .toml_path_pattern = "mcp.servers[].env",
     .env_var_pattern = "YAC_MCP_<ID>_ENV_JSON",
     .value_type = Type::String,
     .default_description = "JSON object of string environment values",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.headers_json",
     .toml_path_pattern = "mcp.servers[].headers",
     .env_var_pattern = "YAC_MCP_<ID>_HEADERS_JSON",
     .value_type = Type::String,
     .default_description = "JSON object of string HTTP header values",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.auth.type",
     .toml_path_pattern = "mcp.servers[].auth.type",
     .env_var_pattern = "YAC_MCP_<ID>_AUTH_TYPE",
     .value_type = Type::String,
     .default_description = "bearer or oauth",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.auth.oauth.authorization_url",
     .toml_path_pattern = "mcp.servers[].auth.authorization_url",
     .env_var_pattern = "YAC_MCP_<ID>_OAUTH_AUTHORIZATION_URL",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.auth.oauth.token_url",
     .toml_path_pattern = "mcp.servers[].auth.token_url",
     .env_var_pattern = "YAC_MCP_<ID>_OAUTH_TOKEN_URL",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.auth.oauth.client_id",
     .toml_path_pattern = "mcp.servers[].auth.client_id",
     .env_var_pattern = "YAC_MCP_<ID>_OAUTH_CLIENT_ID",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::External,
     .docs = kMcpDocs},
    {.key = "mcp.servers.auth.oauth.scopes",
     .toml_path_pattern = "mcp.servers[].auth.scopes",
     .env_var_pattern = "YAC_MCP_<ID>_OAUTH_SCOPES",
     .value_type = Type::StringArray,
     .default_description = "empty array; env override is whitespace-split",
     .classification = Class::External,
     .docs = kMcpDocs},
}};

void AddIfDuplicate(std::unordered_set<std::string_view>& seen,
                    std::string_view value, std::string label,
                    std::vector<SettingsRegistryIssue>& issues) {
  if (value.empty()) {
    return;
  }
  if (!seen.insert(value).second) {
    issues.push_back({.message = "Duplicate " + std::move(label) + ": " +
                                 std::string(value)});
  }
}

}  // namespace

std::span<const SettingClassification> SettingClassifications() {
  return kClassifications;
}

std::span<const SettingMetadata> SettingsRegistryRecords() {
  return kRecords;
}

std::span<const DynamicSettingPattern> DynamicSettingsRegistryPatterns() {
  return kDynamicPatterns;
}

std::vector<SettingsRegistryIssue> ValidateSettingsRegistry(
    std::span<const SettingMetadata> records,
    std::span<const DynamicSettingPattern> dynamic_patterns) {
  std::vector<SettingsRegistryIssue> issues;
  std::unordered_set<std::string_view> static_keys;
  std::unordered_set<std::string_view> toml_paths;
  std::unordered_set<std::string_view> env_vars;
  std::unordered_set<std::string_view> dynamic_keys;
  std::unordered_set<std::string_view> dynamic_toml_patterns;
  std::unordered_set<std::string_view> dynamic_env_patterns;

  for (const auto& record : records) {
    AddIfDuplicate(static_keys, record.key, "static key", issues);
    AddIfDuplicate(toml_paths, record.toml_path, "TOML path", issues);
    AddIfDuplicate(env_vars, record.env_var, "env var", issues);
    if (record.classification == Class::UserFacing &&
        (record.toml_path.empty() || record.env_var.empty())) {
      issues.push_back(
          {.message = "User-facing setting requires TOML and env metadata: " +
                      std::string(record.key)});
    }
  }

  for (const auto& pattern : dynamic_patterns) {
    AddIfDuplicate(dynamic_keys, pattern.key, "dynamic key", issues);
    AddIfDuplicate(dynamic_toml_patterns, pattern.toml_path_pattern,
                   "dynamic TOML pattern", issues);
    AddIfDuplicate(dynamic_env_patterns, pattern.env_var_pattern,
                   "dynamic env pattern", issues);
    if (pattern.classification == Class::UserFacing &&
        (pattern.toml_path_pattern.empty() ||
         pattern.env_var_pattern.empty())) {
      issues.push_back(
          {.message =
               "User-facing dynamic setting requires TOML and env metadata: " +
               std::string(pattern.key)});
    }
  }

  return issues;
}

}  // namespace yac::chat
