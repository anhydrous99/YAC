#include "chat/settings_registry.hpp"

#include "chat/types.hpp"

#include <array>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <utility>

namespace yac::chat {

namespace {

using Class = SettingClassification;
using Docs = SettingsDocExpectation;
using Type = SettingValueType;
using DefaultType = SettingRuntimeDefaultType;
using ValidationType = SettingValidationType;

constexpr Docs kConfigExampleTemplate{.configuration_docs = true,
                                      .settings_example = true,
                                      .default_template = true};
constexpr Docs kConfigExample{.configuration_docs = true,
                              .settings_example = true};
constexpr Docs kOpenAiAuthDocs{.configuration_docs = true,
                               .settings_example = true,
                               .default_template = true,
                               .openai_auth_docs = true};
constexpr Docs kMcpDocs{
    .settings_example = true, .default_template = true, .mcp_docs = true};
constexpr Docs kExampleOnly{.settings_example = true, .default_template = true};

constexpr std::array<SettingClassification, 6> kClassifications = {
    Class::UserFacing, Class::Secret,   Class::Operational,
    Class::External,   Class::Internal, Class::Deprecated};

constexpr std::array<std::string_view, 2> kCompactModeValues = {"summarize",
                                                                "truncate"};
constexpr std::array<std::string_view, 2> kMcpTransportValues = {"stdio",
                                                                 "http"};
constexpr std::array<std::string_view, 2> kMcpAuthTypeValues = {"bearer",
                                                                "oauth"};
constexpr std::array<std::string_view, 6> kReasoningEffortValues = {
    "none", "minimal", "low", "medium", "high", "xhigh"};
constexpr std::array<std::string_view, 1> kWebSearchProviderValues = {"exa"};

constexpr std::array<SettingMetadata, 42> kRecords = {{
    {.key = "temperature",
     .toml_path = "temperature",
     .env_var = "YAC_TEMPERATURE",
     .value_type = Type::Number,
     .default_description = "0.7; valid range 0.0 to 2.0",
     .runtime_default = {.type = DefaultType::Number, .number_value = 0.7},
     .validation = {.type = ValidationType::NumberRange,
                    .min_number = kMinTemperature,
                    .max_number = kMaxTemperature},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "system_prompt",
     .toml_path = "system_prompt",
     .env_var = "YAC_SYSTEM_PROMPT",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "workspace_root",
     .toml_path = "workspace_root",
     .env_var = "YAC_WORKSPACE_ROOT",
     .value_type = Type::String,
     .default_description = "launch current working directory",
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "provider.id",
     .toml_path = "provider.id",
     .env_var = "YAC_PROVIDER",
     .value_type = Type::String,
     .default_description =
         "openai-compatible; provider presets may override dependent defaults",
     .runtime_default = {.type = DefaultType::String,
                         .string_value = "openai-compatible"},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "provider.model",
     .toml_path = "provider.model",
     .env_var = "YAC_MODEL",
     .value_type = Type::String,
     .default_description =
         "gpt-4o-mini; provider presets may supply provider-specific defaults",
     .runtime_default = {.type = DefaultType::String,
                         .string_value = "gpt-4o-mini"},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "provider.model_settings.provider",
     .toml_path = "provider.model_settings.provider",
     .value_type = Type::String,
     .default_description =
         "required exact provider id for scoped model settings",
     .classification = Class::External,
     .docs = kExampleOnly},
    {.key = "provider.model_settings.model",
     .toml_path = "provider.model_settings.model",
     .value_type = Type::String,
     .default_description = "required exact model id for scoped model settings",
     .classification = Class::External,
     .docs = kExampleOnly},
    {.key = "provider.model_settings.effort",
     .toml_path = "provider.model_settings.effort",
     .value_type = Type::String,
     .default_description =
         "unset; valid values are none, minimal, low, medium, high, or xhigh",
     .validation = {.type = ValidationType::StringEnum,
                    .allowed_values = kReasoningEffortValues},
     .classification = Class::External,
     .docs = kExampleOnly},
    {.key = "provider.base_url",
     .toml_path = "provider.base_url",
     .env_var = "YAC_BASE_URL",
     .value_type = Type::String,
     .default_description = "https://api.openai.com/v1/; provider presets may "
                            "supply provider-specific defaults",
     .runtime_default = {.type = DefaultType::String,
                         .string_value = "https://api.openai.com/v1/"},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "provider.api_key_env",
     .toml_path = "provider.api_key_env",
     .env_var = "YAC_API_KEY_ENV",
     .value_type = Type::String,
     .default_description = "OPENAI_API_KEY; provider presets may supply "
                            "provider-specific defaults",
     .runtime_default = {.type = DefaultType::String,
                         .string_value = "OPENAI_API_KEY"},
     .classification = Class::UserFacing,
     .docs = kOpenAiAuthDocs},
    {.key = "provider.context_window",
     .toml_path = "provider.context_window",
     .env_var = "YAC_CONTEXT_WINDOW",
     .value_type = Type::Integer,
     .default_description =
         "0 means auto-detect; valid range 1 to 10000000 when set",
     .runtime_default = {.type = DefaultType::Integer, .integer_value = 0},
     .validation = {.type = ValidationType::IntegerRange,
                    .min_integer = kMinContextWindow,
                    .max_integer = kMaxContextWindow},
     .classification = Class::UserFacing,
     .docs = kConfigExample},
    {.key = "lsp.clangd.command",
     .toml_path = "lsp.clangd.command",
     .env_var = "YAC_LSP_CLANGD_COMMAND",
     .value_type = Type::String,
     .default_description = "clangd",
     .runtime_default = {.type = DefaultType::String, .string_value = "clangd"},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "lsp.clangd.args",
     .toml_path = "lsp.clangd.args",
     .env_var = "YAC_LSP_CLANGD_ARGS",
     .value_type = Type::StringArray,
     .default_description = "empty array; env override is whitespace-split",
     .runtime_default = {.type = DefaultType::StringArray},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "theme.sync_terminal_background",
     .toml_path = "theme.sync_terminal_background",
     .env_var = "YAC_SYNC_TERMINAL_BACKGROUND",
     .value_type = Type::Bool,
     .default_description = "true",
     .runtime_default = {.type = DefaultType::Bool, .bool_value = true},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "theme.name",
     .toml_path = "theme.name",
     .env_var = "YAC_THEME_NAME",
     .value_type = Type::String,
     .default_description = "vivid",
     .runtime_default = {.type = DefaultType::String, .string_value = "vivid"},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "theme.density",
     .toml_path = "theme.density",
     .env_var = "YAC_THEME_DENSITY",
     .value_type = Type::String,
     .default_description = "comfortable",
     .runtime_default = {.type = DefaultType::String,
                         .string_value = "comfortable"},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "compact.auto_enabled",
     .toml_path = "compact.auto_enabled",
     .env_var = "YAC_COMPACT_AUTO_ENABLED",
     .value_type = Type::Bool,
     .default_description = "true",
     .runtime_default = {.type = DefaultType::Bool, .bool_value = true},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "compact.threshold",
     .toml_path = "compact.threshold",
     .env_var = "YAC_COMPACT_THRESHOLD",
     .value_type = Type::Number,
     .default_description = "0.8; valid range 0.05 to 1.0",
     .runtime_default = {.type = DefaultType::Number, .number_value = 0.8},
     .validation = {.type = ValidationType::NumberRange,
                    .min_number = kMinAutoCompactThreshold,
                    .max_number = kMaxAutoCompactThreshold},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "compact.keep_last",
     .toml_path = "compact.keep_last",
     .env_var = "YAC_COMPACT_KEEP_LAST",
     .value_type = Type::Integer,
     .default_description = "20; valid range 1 to 10000",
     .runtime_default = {.type = DefaultType::Integer, .integer_value = 20},
     .validation = {.type = ValidationType::IntegerRange,
                    .min_integer = kMinAutoCompactKeepLast,
                    .max_integer = kMaxAutoCompactKeepLast},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "compact.mode",
     .toml_path = "compact.mode",
     .env_var = "YAC_COMPACT_MODE",
     .value_type = Type::String,
     .default_description = "summarize; valid values are summarize or truncate",
     .runtime_default = {.type = DefaultType::String,
                         .string_value = "summarize"},
     .validation = {.type = ValidationType::StringEnum,
                    .allowed_values = kCompactModeValues},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
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
     .docs = kConfigExampleTemplate},
    {.key = "mcp.result_max_bytes",
     .toml_path = "mcp.result_max_bytes",
     .env_var = "YAC_MCP_RESULT_MAX_BYTES",
     .value_type = Type::Integer,
     .default_description = "262144; env/TOML values must be positive integers",
     .runtime_default = {.type = DefaultType::Integer, .integer_value = 262144},
     .validation = {.type = ValidationType::PositiveInteger},
     .classification = Class::UserFacing,
     .docs = kMcpDocs},
    {.key = "web_search.enabled",
     .toml_path = "web_search.enabled",
     .env_var = "YAC_WEB_SEARCH_ENABLED",
     .value_type = Type::Bool,
     .default_description = "false",
     .runtime_default = {.type = DefaultType::Bool, .bool_value = false},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "web_search.provider",
     .toml_path = "web_search.provider",
     .env_var = "YAC_WEB_SEARCH_PROVIDER",
     .value_type = Type::String,
     .default_description = "exa; only exa is supported in the MVP",
     .runtime_default = {.type = DefaultType::String, .string_value = "exa"},
     .validation = {.type = ValidationType::StringEnum,
                    .allowed_values = kWebSearchProviderValues},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "web_search.endpoint",
     .toml_path = "web_search.endpoint",
     .env_var = "YAC_EXA_ENDPOINT",
     .value_type = Type::String,
     .default_description = "https://api.exa.ai/search",
     .runtime_default = {.type = DefaultType::String,
                         .string_value = "https://api.exa.ai/search"},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "web_search.timeout_seconds",
     .toml_path = "web_search.timeout_seconds",
     .env_var = "YAC_WEB_SEARCH_TIMEOUT_SECONDS",
     .value_type = Type::Integer,
     .default_description = "25; valid range 1 to 120",
     .runtime_default = {.type = DefaultType::Integer, .integer_value = 25},
     .validation = {.type = ValidationType::IntegerRange,
                    .min_integer = kMinWebSearchTimeoutSeconds,
                    .max_integer = kMaxWebSearchTimeoutSeconds},
     .classification = Class::UserFacing,
     .docs = kConfigExampleTemplate},
    {.key = "web_search.result_limit",
     .toml_path = "web_search.result_limit",
     .value_type = Type::Integer,
     .default_description = "5; valid range 1 to 10",
     .runtime_default = {.type = DefaultType::Integer, .integer_value = 5},
     .validation = {.type = ValidationType::IntegerRange,
                    .min_integer = kMinWebSearchResultLimit,
                    .max_integer = kMaxWebSearchResultLimit},
     .classification = Class::External,
     .docs = kExampleOnly},
    {.key = "web_search.context_limit",
     .toml_path = "web_search.context_limit",
     .value_type = Type::Integer,
     .default_description = "4096; valid range 1 to 12000",
     .runtime_default = {.type = DefaultType::Integer, .integer_value = 4096},
     .validation = {.type = ValidationType::IntegerRange,
                    .min_integer = kMinWebSearchContextLimit,
                    .max_integer = kMaxWebSearchContextLimit},
     .classification = Class::External,
     .docs = kExampleOnly},
    {.key = "web_search.exa_api_key_env_value",
     .env_var = "YAC_EXA_API_KEY",
     .value_type = Type::Secret,
     .default_description = "unset; Exa API key for web_search when enabled",
     .classification = Class::Secret,
     .docs = kConfigExampleTemplate},
    {.key = "provider.options.region",
     .toml_path = "provider.options.region",
     .env_var = "YAC_BEDROCK_REGION",
     .value_type = Type::String,
     .default_description = "us-east-1 when provider.id is bedrock",
     .classification = Class::UserFacing,
     .docs = kConfigExample},
    {.key = "bedrock.aws_region_fallback",
     .env_var = "AWS_REGION",
     .value_type = Type::String,
     .default_description = "fallback for provider.options.region when "
                            "YAC_BEDROCK_REGION is unset",
     .classification = Class::External,
     .docs = kConfigExample},
    {.key = "process.home_dir_fallback",
     .env_var = "HOME",
     .value_type = Type::String,
     .default_description = "fallback for resolving config and auth paths",
     .classification = Class::External,
     .docs = {.configuration_docs = true}},
    {.key = "process.path_lookup",
     .env_var = "PATH",
     .value_type = Type::String,
     .default_description = "search path for executables and helper binaries",
     .classification = Class::External,
     .docs = {.configuration_docs = true}},
    {.key = "provider.options.profile",
     .toml_path = "provider.options.profile",
     .env_var = "YAC_BEDROCK_PROFILE",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::UserFacing,
     .docs = kConfigExample},
    {.key = "provider.options.endpoint_override",
     .toml_path = "provider.options.endpoint_override",
     .env_var = "YAC_BEDROCK_ENDPOINT_OVERRIDE",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::UserFacing,
     .docs = kConfigExample},
    {.key = "provider.options.credential_refresh_command",
     .toml_path = "provider.options.credential_refresh_command",
     .env_var = "YAC_BEDROCK_CREDENTIAL_REFRESH_COMMAND",
     .value_type = Type::String,
     .default_description = "unset",
     .classification = Class::UserFacing,
     .docs = kConfigExample},
    {.key = "provider.options.max_tokens",
     .toml_path = "provider.options.max_tokens",
     .env_var = "YAC_BEDROCK_MAX_TOKENS",
     .value_type = Type::Integer,
     .default_description =
         "4096 when provider.id is bedrock; valid range 1 to 200000",
     .runtime_default = {.type = DefaultType::Integer, .integer_value = 4096},
     .validation = {.type = ValidationType::IntegerRange,
                    .min_integer = 1,
                    .max_integer = 200000},
     .classification = Class::UserFacing,
     .docs = kConfigExample},
    {.key = "openai.auth_store",
     .env_var = "YAC_OPENAI_AUTH_STORE",
     .value_type = Type::String,
     .default_description =
         "legacy file auth selector for OpenAI auth import paths",
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
     .validation = {.type = ValidationType::StringEnum,
                    .allowed_values = kMcpTransportValues},
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
     .runtime_default = {.type = DefaultType::Bool, .bool_value = true},
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
     .runtime_default = {.type = DefaultType::Bool, .bool_value = false},
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
     .validation = {.type = ValidationType::StringEnum,
                    .allowed_values = kMcpAuthTypeValues},
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

bool RuntimeDefaultMatchesValueType(
    const SettingRuntimeDefault& runtime_default, Type value_type) {
  switch (runtime_default.type) {
    case DefaultType::None:
    case DefaultType::Unset:
      return true;
    case DefaultType::String:
      return value_type == Type::String || value_type == Type::Secret;
    case DefaultType::StringArray:
      return value_type == Type::StringArray;
    case DefaultType::Bool:
      return value_type == Type::Bool;
    case DefaultType::Integer:
      return value_type == Type::Integer;
    case DefaultType::Number:
      return value_type == Type::Number;
  }
  return false;
}

bool ValidationMatchesValueType(const SettingValidationMetadata& validation,
                                Type value_type) {
  switch (validation.type) {
    case ValidationType::None:
    case ValidationType::Required:
      return true;
    case ValidationType::NumberRange:
      return value_type == Type::Number;
    case ValidationType::IntegerRange:
    case ValidationType::PositiveInteger:
      return value_type == Type::Integer;
    case ValidationType::StringEnum:
      return value_type == Type::String;
  }
  return false;
}

void ValidateRuntimeMetadata(std::string_view key, Type value_type,
                             const SettingRuntimeDefault& runtime_default,
                             const SettingValidationMetadata& validation,
                             std::vector<SettingsRegistryIssue>& issues) {
  if (!RuntimeDefaultMatchesValueType(runtime_default, value_type)) {
    issues.push_back({.message = "Runtime default type does not match value "
                                 "type: " +
                                 std::string(key)});
  }
  if (!ValidationMatchesValueType(validation, value_type)) {
    issues.push_back({.message = "Validation metadata type does not match "
                                 "value type: " +
                                 std::string(key)});
  }
  if (validation.type == ValidationType::NumberRange &&
      validation.min_number > validation.max_number) {
    issues.push_back(
        {.message = "Invalid numeric validation range: " + std::string(key)});
  }
  if (validation.type == ValidationType::IntegerRange &&
      validation.min_integer > validation.max_integer) {
    issues.push_back(
        {.message = "Invalid integer validation range: " + std::string(key)});
  }
  if (validation.type == ValidationType::StringEnum &&
      validation.allowed_values.empty()) {
    issues.push_back({.message = "String enum validation has no values: " +
                                 std::string(key)});
  }
}

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

const SettingMetadata& TemplateRecord(std::string_view key) {
  const auto* record = FindSettingsRegistryRecord(key);
  if (record == nullptr) {
    static constexpr SettingMetadata kMissing{};
    return kMissing;
  }
  return *record;
}

std::string FormatNumber(double value) {
  std::ostringstream output;
  output << value;
  return output.str();
}

std::optional<std::string> TomlLiteral(const SettingRuntimeDefault& value) {
  switch (value.type) {
    case DefaultType::String:
      return "\"" + std::string(value.string_value) + "\"";
    case DefaultType::StringArray:
      return "[]";
    case DefaultType::Bool:
      return value.bool_value ? "true" : "false";
    case DefaultType::Integer:
      return std::to_string(value.integer_value);
    case DefaultType::Number:
      return FormatNumber(value.number_value);
    case DefaultType::None:
    case DefaultType::Unset:
      return std::nullopt;
  }
  return std::nullopt;
}

std::string DefaultLiteral(std::string_view key) {
  const auto literal = TomlLiteral(TemplateRecord(key).runtime_default);
  return literal.value_or("\"\"");
}

std::string Comment(std::string_view key) {
  return std::string(TemplateRecord(key).default_description);
}

void AppendTemplateHeader(std::string& output, bool repository_example) {
  output += "# YAC configuration.\n#\n";
  if (repository_example) {
    output +=
        "# On first launch, YAC writes this file to "
        "~/.yac/settings.toml. This copy\n"
        "# under the repo root is for reference. To reset your "
        "configuration, remove\n"
        "# ~/.yac/settings.toml and start YAC again.\n#\n"
        "# Shell environment variables named YAC_* override anything "
        "set here at\n"
        "# startup, which is convenient for CI and per-shell "
        "experiments.\n"
        "# Runtime state lives in ~/.yac/state.sqlite. Effective settings "
        "use:\n"
        "# env > TOML > SQLite > built-in defaults.\n";
  } else {
    output +=
        "# This file was auto-generated on first launch. Edit any value "
        "and restart\n"
        "# YAC to pick up the change. Shell environment variables "
        "named YAC_* override\n"
        "# anything set here at startup, which is convenient for CI "
        "and per-shell\n"
        "# experiments.\n"
        "# Runtime state lives in ~/.yac/state.sqlite. Effective settings "
        "use:\n"
        "# env > TOML > SQLite > built-in defaults.\n";
  }
  output +=
      "#\n"
      "# Secrets: prefer OPENAI_API_KEY, ZAI_API_KEY, or stored "
      "OpenAI auth over\n"
      "# placing a key in [provider].api_key below. For OpenAI, "
      "precedence is:\n"
      "# env > TOML > SQLite > built-in defaults.\n"
      "# Plaintext provider.api_key values remain supported as overrides, "
      "but are not\n"
      "# migrated into SQLite.\n"
      "# Store an OpenAI API key without shell history exposure:\n"
      "#   printf 'sk-...' | yac auth openai set-api-key --stdin\n\n";
}

void AppendProviderExamples(std::string& output) {
  output +=
      "\n# OpenAI provider auth is opt-in: set provider.id = \"openai\" to "
      "use stored\n"
      "# OpenAI auth or browser OAuth. With the default openai-compatible "
      "provider,\n"
      "# OPENAI_API_KEY is used as a normal OpenAI-compatible API key.\n"
      "#\n"
      "# OpenAI auth options when provider.id = \"openai\":\n"
      "# - Use OPENAI_API_KEY for the highest-priority API-key path.\n"
      "# - Run `yac auth openai login` for browser OAuth.\n"
      "# - Run `yac auth openai login --device` for device OAuth in "
      "headless shells.\n"
      "# - Run `yac auth openai status` to see the effective source.\n"
      "# - Run `yac auth openai logout` to clear stored OpenAI auth.\n"
      "# Stored OpenAI auth uses the SQLite state store at "
      "~/.yac/state.sqlite.\n"
      "# SQLite also stores provider profiles, credentials, and last-used "
      "runtime\n"
      "# state. Legacy keychain/file auth may be imported best-effort and "
      "left\n"
      "# untouched. YAC_OPENAI_AUTH_STORE=file applies only to legacy file "
      "auth\n"
      "# access during import paths. Stock SQLite is protected by local "
      "file\n"
      "# permissions and is not encrypted.\n\n"
      "# OpenAI provider opt-in example.\n"
      "# [provider]\n"
      "# id          = \"openai\"\n"
      "# model       = " +
      DefaultLiteral("provider.model") +
      "\n"
      "# base_url    = " +
      DefaultLiteral("provider.base_url") +
      "\n"
      "# api_key_env = " +
      DefaultLiteral("provider.api_key_env") +
      "\n\n"
      "# Generic OpenAI-compatible endpoint example for other providers.\n"
      "# [provider]\n"
      "# id          = " +
      DefaultLiteral("provider.id") +
      "\n"
      "# model       = " +
      DefaultLiteral("provider.model") +
      "\n"
      "# base_url    = " +
      DefaultLiteral("provider.base_url") +
      "\n"
      "# api_key_env = " +
      DefaultLiteral("provider.api_key_env") +
      "\n\n"
      "# Z.ai preset example: uncomment to switch providers.\n"
      "# Leaving model/base_url/api_key_env unset applies the built-in zai "
      "defaults\n"
      "# (glm-5.1, https://api.z.ai/api/coding/paas/v4, ZAI_API_KEY).\n"
      "# [provider]\n"
      "# id = \"zai\"\n\n"
      "# Bedrock provider: set id = \"bedrock\" to use AWS Bedrock "
      "(Converse API).\n"
      "# Credentials: AWS SDK default chain (env vars, ~/.aws/credentials, "
      "IAM role, SSO).\n"
      "# [provider]\n"
      "# id          = \"bedrock\"\n"
      "# model       = \"anthropic.claude-3-5-haiku-20241022-v1:0\"\n"
      "# options.region             = \"us-east-1\"   # or set "
      "AWS_REGION / YAC_BEDROCK_REGION\n"
      "# options.max_tokens         = \"4096\"\n"
      "# options.profile            = \"\"            # AWS profile "
      "(optional)\n"
      "# options.endpoint_override  = \"\"            # VPC endpoint "
      "(optional)\n"
      "# options.credential_refresh_command = \"\"    # command to run on "
      "auth failure before retrying; also settable via "
      "YAC_BEDROCK_CREDENTIAL_REFRESH_COMMAND\n";
}

std::string GenerateSettingsToml(bool repository_example) {
  std::string output;
  AppendTemplateHeader(output, repository_example);
  output += "temperature = " + DefaultLiteral("temperature") + "\n";
  output += "# system_prompt = \"You are a helpful assistant.\"\n";
  output += "# workspace_root = \"/path/to/workspace\"   # defaults to " +
            Comment("workspace_root") + "\n\n";

  output += "[provider]\n";
  output += "id          = " + DefaultLiteral("provider.id") +
            "         # \"openai-compatible\", \"openai\", or \"zai\"\n";
  output += "model       = " + DefaultLiteral("provider.model") + "\n";
  output += "base_url    = " + DefaultLiteral("provider.base_url") + "\n";
  output += "api_key_env = " + DefaultLiteral("provider.api_key_env") +
            "            # env var that holds the key\n";
  output += "# api_key        = \"\"                     # optional; " +
            Comment("provider.api_key") + "\n";
  output += "# context_window = " + DefaultLiteral("provider.context_window") +
            "                      # override context window size (tokens); " +
            Comment("provider.context_window") + "\n";
  output +=
      "\n# Optional exact provider+model scoped reasoning effort. Omit effort "
      "to "
      "use\n"
      "# the provider default; valid values: none, minimal, low, medium, "
      "high, xhigh.\n"
      "# [[provider.model_settings]]\n"
      "# provider = \"openai\"\n"
      "# model = \"gpt-5.5\"\n"
      "# effort = \"medium\"\n";

  AppendProviderExamples(output);

  output += "\n[lsp.clangd]\n";
  output += "command = " + DefaultLiteral("lsp.clangd.command") + "\n";
  output += "args    = " + DefaultLiteral("lsp.clangd.args") + "\n\n";

  output += "[theme]\n";
  output +=
      "# Active theme preset. Built-in: \"vivid\" (default), "
      "\"system\".\n"
      "# The \"system\" theme uses terminal default colors and\n"
      "# disables OSC 11 background sync.\n";
  output += "name = " + DefaultLiteral("theme.name") + "\n\n";
  output +=
      "# Theme density: \"comfortable\" (default, normal spacing) or "
      "\"compact\"\n"
      "# (tighter layout).\n";
  output += "# density = " + DefaultLiteral("theme.density") + "\n\n";
  output +=
      "# Paint the terminal background to match the active theme's "
      "canvas\n"
      "# color (via OSC 11) and restore the terminal default on exit. "
      "Has\n"
      "# no effect when name = \"system\".\n";
  output += "sync_terminal_background = " +
            DefaultLiteral("theme.sync_terminal_background") + "\n\n";

  output += "[compact]\n";
  output +=
      "# Auto-compact the conversation history when projected "
      "prompt-token usage\n"
      "# crosses `threshold` x the model's context window. Fired "
      "before each new\n"
      "# user prompt; never splits a tool_call/tool_result pair.\n";
  output += "auto_enabled = " + DefaultLiteral("compact.auto_enabled") + "\n";
  output += "threshold    = " + DefaultLiteral("compact.threshold") +
            "                       # 0.05 .. 1.0\n";
  output += "keep_last    = " + DefaultLiteral("compact.keep_last") +
            "                        # most-recent messages to keep\n";
  output += "mode         = " + DefaultLiteral("compact.mode") +
            "               # \"summarize\" (default) or \"truncate\"\n\n";

  output += "[web_search]\n";
  output +=
      "# Enables the built-in web_search tool. The MVP provider is fixed "
      "to Exa.\n";
  output += "enabled = " + DefaultLiteral("web_search.enabled") + "\n";
  output += "provider = " + DefaultLiteral("web_search.provider") +
            "                 # only \"exa\" is supported\n";
  output += "endpoint = " + DefaultLiteral("web_search.endpoint") + "\n";
  output +=
      "timeout_seconds = " + DefaultLiteral("web_search.timeout_seconds") +
      "             # 1 .. 120; set YAC_EXA_API_KEY when enabled\n";
  output += "# result_limit = " + DefaultLiteral("web_search.result_limit") +
            "                  # 1 .. 10 results passed to the executor\n";
  output += "# context_limit = " + DefaultLiteral("web_search.context_limit") +
            "              # 1 .. 12000 context chars per result\n\n";

  output += "[mcp]\n";
  output += "# Maximum bytes for MCP tool result payloads (default: " +
            DefaultLiteral("mcp.result_max_bytes") + " = 256 KB).\n";
  output +=
      "# result_max_bytes = " + DefaultLiteral("mcp.result_max_bytes") + "\n\n";
  output +=
      "# MCP server examples (all commented; uncomment to enable).\n"
      "#\n"
      "# Stdio example: Context7 via npx (requires Node.js).\n"
      "# [[mcp.servers]]\n"
      "# id = \"context7\"\n"
      "# transport = \"stdio\"\n"
      "# command = \"npx\"\n"
      "# args = [\"-y\", \"@upstash/context7-mcp\"]\n"
      "# enabled = true\n"
      "# auto_start = true\n"
      "#\n"
      "# HTTP+OAuth example: Generic OAuth2 server.\n"
      "# [[mcp.servers]]\n"
      "# id = \"example-oauth\"\n"
      "# transport = \"http\"\n"
      "# url = \"https://mcp.example.com/api\"\n"
      "# enabled = true\n"
      "# auto_start = false\n"
      "# requires_approval = false\n"
      "# [mcp.servers.auth]\n"
      "# type = \"oauth\"\n"
      "# authorization_url = \"https://auth.example.com/authorize\"\n"
      "# token_url = \"https://auth.example.com/token\"\n"
      "# client_id = \"your-client-id\"\n"
      "# scopes = [\"read\", \"write\"]\n";
  return output;
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

const SettingMetadata* FindSettingsRegistryRecord(std::string_view key) {
  for (const auto& record : kRecords) {
    if (record.key == key) {
      return &record;
    }
  }
  return nullptr;
}

const DynamicSettingPattern* FindDynamicSettingsRegistryPattern(
    std::string_view key) {
  for (const auto& pattern : kDynamicPatterns) {
    if (pattern.key == key) {
      return &pattern;
    }
  }
  return nullptr;
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
    ValidateRuntimeMetadata(record.key, record.value_type,
                            record.runtime_default, record.validation, issues);
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
    ValidateRuntimeMetadata(pattern.key, pattern.value_type,
                            pattern.runtime_default, pattern.validation,
                            issues);
  }

  return issues;
}

std::string GenerateDefaultSettingsTomlTemplate() {
  return GenerateSettingsToml(false);
}

std::string GenerateSettingsExampleToml() {
  return GenerateSettingsToml(true);
}

}  // namespace yac::chat
