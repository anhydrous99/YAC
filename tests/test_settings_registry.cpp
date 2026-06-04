#include "chat/settings_registry.hpp"
#include "chat/settings_toml_template.hpp"
#include "config_env_test_helpers.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <regex>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::chat::DynamicSettingPattern;
using yac::chat::DynamicSettingsRegistryPatterns;
using yac::chat::GenerateDefaultSettingsTomlTemplate;
using yac::chat::GenerateSettingsExampleToml;
using yac::chat::SettingClassification;
using yac::chat::SettingClassifications;
using yac::chat::SettingMetadata;
using yac::chat::SettingsRegistryRecords;
using yac::chat::ValidateSettingsRegistry;

#ifndef README_MD_PATH
#error "README_MD_PATH must point at README.md"
#endif

#ifndef SETTINGS_EXAMPLE_TOML_PATH
#error "SETTINGS_EXAMPLE_TOML_PATH must point at settings.example.toml"
#endif

#ifndef CONFIGURATION_DOCS_PATH
#error "CONFIGURATION_DOCS_PATH must point at docs/configuration.md"
#endif

#ifndef MCP_DOCS_PATH
#error "MCP_DOCS_PATH must point at docs/mcp.md"
#endif

#ifndef OPENAI_AUTH_DOCS_PATH
#error "OPENAI_AUTH_DOCS_PATH must point at docs/openai-auth.md"
#endif

#define YAC_STRINGIFY_DETAIL(value) #value
#define YAC_STRINGIFY(value) YAC_STRINGIFY_DETAIL(value)

namespace {

struct DocTokens {
  std::set<std::string> toml_paths;
  std::set<std::string> env_vars;
};

struct DocValidationIssue {
  std::string message;
};

bool HasIssue(std::span<const yac::chat::SettingsRegistryIssue> issues,
              std::string_view text) {
  return std::ranges::any_of(issues, [&](const auto& issue) {
    return issue.message.find(text) != std::string::npos;
  });
}

const SettingMetadata* FindRecord(std::span<const SettingMetadata> records,
                                  std::string_view key) {
  const auto it = std::ranges::find_if(
      records,
      [&](const SettingMetadata& record) { return record.key == key; });
  if (it == records.end()) {
    return nullptr;
  }
  return &*it;
}

std::vector<SettingMetadata> MutableRecords() {
  const auto records = SettingsRegistryRecords();
  return {records.begin(), records.end()};
}

std::vector<DynamicSettingPattern> MutableDynamicPatterns() {
  const auto patterns = DynamicSettingsRegistryPatterns();
  return {patterns.begin(), patterns.end()};
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

std::string Trim(std::string value) {
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value[0]))) {
    value.erase(value.begin());
  }
  while (!value.empty() &&
         std::isspace(static_cast<unsigned char>(value.back()))) {
    value.pop_back();
  }
  return value;
}

std::string NormalizeTomlTable(std::string table) {
  if (table == "mcp.servers") {
    return "mcp.servers[]";
  }
  constexpr std::string_view kMcpServerPrefix = "mcp.servers.";
  if (table.starts_with(kMcpServerPrefix)) {
    return "mcp.servers[]." + table.substr(kMcpServerPrefix.size());
  }
  return table;
}

std::optional<std::string> ExtractTomlKey(std::string line) {
  line = Trim(std::move(line));
  if (line.empty()) {
    return std::nullopt;
  }
  if (line[0] == '#') {
    line.erase(line.begin());
    line = Trim(std::move(line));
  }
  if (line.empty() || line[0] == '[') {
    return std::nullopt;
  }

  const auto equals = line.find('=');
  if (equals == std::string::npos) {
    return std::nullopt;
  }
  auto key = Trim(line.substr(0, equals));
  if (key.empty()) {
    return std::nullopt;
  }
  const bool key_chars_ok = std::ranges::all_of(key, [](char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_' ||
           ch == '.';
  });
  if (!key_chars_ok) {
    return std::nullopt;
  }
  return key;
}

DocTokens ExtractDocTokens(std::string_view content) {
  DocTokens tokens;
  std::string current_table;
  std::regex env_regex(
      R"(\b(?:YAC|OPENAI|ZAI|AWS)_[A-Z0-9_<>]+\b|\bHOME\b|\bPATH\b)");
  std::regex toml_path_regex(
      R"(`((?:(?:provider|lsp|theme|compact|mcp)\.[A-Za-z0-9_\.\[\]]+)|temperature|system_prompt|workspace_root)`)");

  std::string text{content};
  for (auto it = std::sregex_iterator(text.begin(), text.end(), env_regex);
       it != std::sregex_iterator(); ++it) {
    tokens.env_vars.insert(it->str());
  }
  for (auto it =
           std::sregex_iterator(text.begin(), text.end(), toml_path_regex);
       it != std::sregex_iterator(); ++it) {
    tokens.toml_paths.insert((*it)[1].str());
  }

  std::istringstream stream(text);
  std::string line;
  while (std::getline(stream, line)) {
    auto trimmed = Trim(line);
    if (trimmed.starts_with("#")) {
      trimmed = Trim(trimmed.substr(1));
    }
    if (trimmed.starts_with("[[") && trimmed.ends_with("]]")) {
      current_table =
          NormalizeTomlTable(Trim(trimmed.substr(2, trimmed.size() - 4)));
      continue;
    }
    if (trimmed.starts_with("[") && trimmed.ends_with("]")) {
      current_table =
          NormalizeTomlTable(Trim(trimmed.substr(1, trimmed.size() - 2)));
      continue;
    }

    auto key = ExtractTomlKey(line);
    if (!key) {
      continue;
    }
    if (*key == "description" || *key == "prompt") {
      continue;
    }
    if (current_table == "mcp.servers[].env" ||
        current_table == "mcp.servers[].headers") {
      tokens.toml_paths.insert(current_table);
      continue;
    }
    if (!current_table.empty() && key->find('.') == std::string::npos) {
      tokens.toml_paths.insert(current_table + "." + *key);
    } else if (!current_table.empty() && current_table == "provider" &&
               key->starts_with("options.")) {
      tokens.toml_paths.insert(current_table + "." + *key);
    } else if (!current_table.empty() && current_table.starts_with("mcp.")) {
      tokens.toml_paths.insert(current_table + "." + *key);
    } else {
      tokens.toml_paths.insert(*key);
    }
  }

  return tokens;
}

std::string RegexForDynamicEnv(std::string_view pattern) {
  std::string regex = "^";
  for (size_t i = 0; i < pattern.size();) {
    if (pattern.substr(i, 4) == "<ID>") {
      regex += "[A-Z0-9]+(?:_[A-Z0-9]+)*";
      i += 4;
      continue;
    }
    const char ch = pattern[i++];
    if (std::isalnum(static_cast<unsigned char>(ch)) || ch == '_') {
      regex += ch;
    } else {
      regex += '\\';
      regex += ch;
    }
  }
  regex += "$";
  return regex;
}

bool MatchesDynamicEnv(std::string_view env_var,
                       std::span<const DynamicSettingPattern> patterns) {
  return std::ranges::any_of(
      patterns, [&](const DynamicSettingPattern& pattern) {
        return !pattern.env_var_pattern.empty() &&
               std::regex_match(
                   std::string(env_var),
                   std::regex(RegexForDynamicEnv(pattern.env_var_pattern)));
      });
}

bool MatchesDynamicToml(std::string_view toml_path,
                        std::span<const DynamicSettingPattern> patterns) {
  return std::ranges::any_of(patterns,
                             [&](const DynamicSettingPattern& pattern) {
                               return pattern.toml_path_pattern == toml_path;
                             });
}

std::vector<DocValidationIssue> ValidateDocTokens(
    const DocTokens& tokens, std::span<const SettingMetadata> records,
    std::span<const DynamicSettingPattern> patterns,
    bool yac::chat::SettingsDocExpectation::* expectation) {
  std::vector<DocValidationIssue> issues;
  const bool readme = expectation == &yac::chat::SettingsDocExpectation::readme;
  const bool configuration_docs =
      expectation == &yac::chat::SettingsDocExpectation::configuration_docs;
  const bool mcp_docs =
      expectation == &yac::chat::SettingsDocExpectation::mcp_docs;
  const bool openai_auth_docs =
      expectation == &yac::chat::SettingsDocExpectation::openai_auth_docs;

  for (const auto& env_var : tokens.env_vars) {
    if (env_var.find('<') != std::string::npos) {
      continue;
    }
    const bool registered = std::ranges::any_of(
        records, [&](const auto& record) { return record.env_var == env_var; });
    if (!registered && !MatchesDynamicEnv(env_var, patterns)) {
      issues.push_back({.message = "Unregistered env var: " + env_var});
    }
  }

  for (const auto& toml_path : tokens.toml_paths) {
    const bool registered = std::ranges::any_of(
        records,
        [&](const auto& record) { return record.toml_path == toml_path; });
    if (!registered && !MatchesDynamicToml(toml_path, patterns)) {
      issues.push_back({.message = "Unregistered TOML key: " + toml_path});
    }
  }

  for (const auto& record : records) {
    if (!(record.docs.*expectation)) {
      continue;
    }
    const bool require_toml = !record.toml_path.empty();
    const bool require_env = !record.env_var.empty() &&
                             (readme || configuration_docs || mcp_docs ||
                              openai_auth_docs || record.toml_path.empty());
    if (require_toml &&
        !tokens.toml_paths.contains(std::string(record.toml_path))) {
      issues.push_back(
          {.message = "Missing TOML key: " + std::string(record.toml_path)});
    }
    if (require_env && !tokens.env_vars.contains(std::string(record.env_var))) {
      issues.push_back(
          {.message = "Missing env var: " + std::string(record.env_var)});
    }
  }

  for (const auto& pattern : patterns) {
    if (!(pattern.docs.*expectation)) {
      continue;
    }
    const bool require_toml =
        !pattern.toml_path_pattern.empty() && pattern.env_var_pattern.empty();
    const bool require_env =
        !pattern.env_var_pattern.empty() && (readme || mcp_docs);
    if (require_toml &&
        !tokens.toml_paths.contains(std::string(pattern.toml_path_pattern))) {
      issues.push_back({.message = "Missing TOML key: " +
                                   std::string(pattern.toml_path_pattern)});
    }
    if (require_env &&
        !tokens.env_vars.contains(std::string(pattern.env_var_pattern))) {
      issues.push_back({.message = "Missing env var: " +
                                   std::string(pattern.env_var_pattern)});
    }
  }

  return issues;
}

bool HasDocIssue(std::span<const DocValidationIssue> issues,
                 std::string_view text) {
  return std::ranges::any_of(issues, [&](const auto& issue) {
    return issue.message.find(text) != std::string::npos;
  });
}

void RequireNoDocIssues(std::span<const DocValidationIssue> issues) {
  for (const auto& issue : issues) {
    FAIL_CHECK(issue.message);
  }
  REQUIRE(issues.empty());
}

}  // namespace

TEST_CASE("settings registry exposes exactly the planned classifications") {
  constexpr std::array expected = {
      SettingClassification::UserFacing,  SettingClassification::Secret,
      SettingClassification::Operational, SettingClassification::External,
      SettingClassification::Internal,    SettingClassification::Deprecated};

  const auto actual = SettingClassifications();
  REQUIRE(actual.size() == expected.size());
  for (const auto classification : expected) {
    INFO("classification index is present");
    REQUIRE(std::ranges::find(actual, classification) != actual.end());
  }
}

TEST_CASE("settings registry production metadata satisfies invariants") {
  const auto issues = ValidateSettingsRegistry(
      SettingsRegistryRecords(), DynamicSettingsRegistryPatterns());
  for (const auto& issue : issues) {
    INFO(issue.message);
  }
  REQUIRE(issues.empty());
}

TEST_CASE("settings registry exposes typed runtime defaults") {
  const auto* temperature =
      FindRecord(SettingsRegistryRecords(), "temperature");
  REQUIRE(temperature != nullptr);
  REQUIRE(temperature->runtime_default.type ==
          yac::chat::SettingRuntimeDefaultType::Number);
  REQUIRE(temperature->runtime_default.number_value == 0.7);
  REQUIRE(temperature->default_description.find("0.7") !=
          std::string_view::npos);

  const auto* context_window =
      FindRecord(SettingsRegistryRecords(), "provider.context_window");
  REQUIRE(context_window != nullptr);
  REQUIRE(context_window->runtime_default.type ==
          yac::chat::SettingRuntimeDefaultType::Integer);
  REQUIRE(context_window->runtime_default.integer_value == 0);

  const auto* sync_background =
      FindRecord(SettingsRegistryRecords(), "theme.sync_terminal_background");
  REQUIRE(sync_background != nullptr);
  REQUIRE(sync_background->runtime_default.type ==
          yac::chat::SettingRuntimeDefaultType::Bool);
  REQUIRE(sync_background->runtime_default.bool_value);

  const auto* compact_threshold =
      FindRecord(SettingsRegistryRecords(), "compact.threshold");
  REQUIRE(compact_threshold != nullptr);
  REQUIRE(compact_threshold->runtime_default.type ==
          yac::chat::SettingRuntimeDefaultType::Number);
  REQUIRE(compact_threshold->runtime_default.number_value == 0.8);

  const auto patterns = DynamicSettingsRegistryPatterns();
  const auto enabled =
      std::ranges::find_if(patterns, [](const DynamicSettingPattern& pattern) {
        return pattern.key == "mcp.servers.enabled";
      });
  REQUIRE(enabled != patterns.end());
  REQUIRE(enabled->runtime_default.type ==
          yac::chat::SettingRuntimeDefaultType::Bool);
  REQUIRE(enabled->runtime_default.bool_value);
}

TEST_CASE("settings registry exposes validation metadata") {
  const auto* temperature =
      FindRecord(SettingsRegistryRecords(), "temperature");
  REQUIRE(temperature != nullptr);
  REQUIRE(temperature->validation.type ==
          yac::chat::SettingValidationType::NumberRange);
  REQUIRE(temperature->validation.min_number == 0.0);
  REQUIRE(temperature->validation.max_number == 2.0);

  const auto* context_window =
      FindRecord(SettingsRegistryRecords(), "provider.context_window");
  REQUIRE(context_window != nullptr);
  REQUIRE(context_window->validation.type ==
          yac::chat::SettingValidationType::IntegerRange);
  REQUIRE(context_window->validation.min_integer == 1);
  REQUIRE(context_window->validation.max_integer == 10000000);

  const auto* compact_threshold =
      FindRecord(SettingsRegistryRecords(), "compact.threshold");
  REQUIRE(compact_threshold != nullptr);
  REQUIRE(compact_threshold->validation.type ==
          yac::chat::SettingValidationType::NumberRange);
  REQUIRE(compact_threshold->validation.min_number == 0.05);
  REQUIRE(compact_threshold->validation.max_number == 1.0);

  const auto patterns = DynamicSettingsRegistryPatterns();
  const auto transport =
      std::ranges::find_if(patterns, [](const DynamicSettingPattern& pattern) {
        return pattern.key == "mcp.servers.transport";
      });
  REQUIRE(transport != patterns.end());
  REQUIRE(transport->validation.type ==
          yac::chat::SettingValidationType::StringEnum);
  REQUIRE(transport->validation.allowed_values.size() == 2);
  REQUIRE(transport->validation.allowed_values[0] == "stdio");
  REQUIRE(transport->validation.allowed_values[1] == "http");
}

TEST_CASE("registry helper tracks every user-facing env var") {
  const auto helper_env_vars = yac::testing::RegistryUserFacingEnvVars();
  std::vector<std::string> expected_env_vars;

  for (const auto& record : SettingsRegistryRecords()) {
    if (record.classification == SettingClassification::UserFacing &&
        !record.env_var.empty()) {
      expected_env_vars.emplace_back(record.env_var);
    }
  }

  std::sort(expected_env_vars.begin(), expected_env_vars.end());
  expected_env_vars.erase(
      std::unique(expected_env_vars.begin(), expected_env_vars.end()),
      expected_env_vars.end());

  REQUIRE(helper_env_vars == expected_env_vars);
}

TEST_CASE("README settings summary uses only registered settings tokens") {
  const auto tokens = ExtractDocTokens(ReadFile(YAC_STRINGIFY(README_MD_PATH)));
  const auto issues = ValidateDocTokens(
      tokens, SettingsRegistryRecords(), DynamicSettingsRegistryPatterns(),
      &yac::chat::SettingsDocExpectation::readme);

  RequireNoDocIssues(issues);
}

TEST_CASE("configuration docs match registry-documented settings") {
  const auto docs = ReadFile(YAC_STRINGIFY(CONFIGURATION_DOCS_PATH));
  const auto tokens = ExtractDocTokens(docs);
  const auto issues = ValidateDocTokens(
      tokens, SettingsRegistryRecords(), DynamicSettingsRegistryPatterns(),
      &yac::chat::SettingsDocExpectation::configuration_docs);

  REQUIRE(docs.find("[[provider.model_settings]]") != std::string::npos);
  REQUIRE(docs.find("OpenAI") != std::string::npos);
  REQUIRE(docs.find("Bedrock Claude thinking controls are not implemented by "
                    "/effort.") != std::string::npos);
  RequireNoDocIssues(issues);
}

TEST_CASE("settings.example.toml matches registry-documented settings") {
  const auto tokens =
      ExtractDocTokens(ReadFile(YAC_STRINGIFY(SETTINGS_EXAMPLE_TOML_PATH)));
  const auto issues = ValidateDocTokens(
      tokens, SettingsRegistryRecords(), DynamicSettingsRegistryPatterns(),
      &yac::chat::SettingsDocExpectation::settings_example);

  RequireNoDocIssues(issues);
}

TEST_CASE(
    "default settings TOML template matches registry-documented settings") {
  const auto tokens = ExtractDocTokens(yac::chat::kDefaultSettingsToml);
  const auto issues = ValidateDocTokens(
      tokens, SettingsRegistryRecords(), DynamicSettingsRegistryPatterns(),
      &yac::chat::SettingsDocExpectation::default_template);

  RequireNoDocIssues(issues);
}

TEST_CASE("settings templates are generated from registry metadata") {
  const std::string settings_example =
      ReadFile(YAC_STRINGIFY(SETTINGS_EXAMPLE_TOML_PATH));

  REQUIRE(yac::chat::kDefaultSettingsToml ==
          GenerateDefaultSettingsTomlTemplate());
  REQUIRE(settings_example == GenerateSettingsExampleToml());

  const auto* provider_id =
      FindRecord(SettingsRegistryRecords(), "provider.id");
  const auto* model = FindRecord(SettingsRegistryRecords(), "provider.model");
  const auto* temperature =
      FindRecord(SettingsRegistryRecords(), "temperature");
  const auto* compact_mode =
      FindRecord(SettingsRegistryRecords(), "compact.mode");
  REQUIRE(provider_id != nullptr);
  REQUIRE(model != nullptr);
  REQUIRE(temperature != nullptr);
  REQUIRE(compact_mode != nullptr);
  const auto* model_effort =
      FindRecord(SettingsRegistryRecords(), "provider.model_settings.effort");
  REQUIRE(model_effort != nullptr);
  REQUIRE(model_effort->toml_path == "provider.model_settings.effort");
  REQUIRE(model_effort->env_var.empty());
  REQUIRE(model_effort->value_type == yac::chat::SettingValueType::String);
  REQUIRE(model_effort->validation.type ==
          yac::chat::SettingValidationType::StringEnum);

  REQUIRE(yac::chat::kDefaultSettingsToml.find(
              std::string{"id          = \""} +
              std::string{provider_id->runtime_default.string_value} + "\"") !=
          std::string::npos);
  REQUIRE(yac::chat::kDefaultSettingsToml.find(
              std::string{"model       = \""} +
              std::string{model->runtime_default.string_value} + "\"") !=
          std::string::npos);
  REQUIRE(yac::chat::kDefaultSettingsToml.find(
              std::string{"temperature = "} +
              std::to_string(temperature->runtime_default.number_value)
                  .substr(0, 3)) != std::string::npos);
  REQUIRE(yac::chat::kDefaultSettingsToml.find(
              std::string{"mode         = \""} +
              std::string{compact_mode->runtime_default.string_value} + "\"") !=
          std::string::npos);
  REQUIRE(yac::chat::kDefaultSettingsToml.find(
              "# [[provider.model_settings]]") != std::string::npos);
  REQUIRE(yac::chat::kDefaultSettingsToml.find("# effort = \"medium\"") !=
          std::string::npos);
}

TEST_CASE("settings registry represents provider model effort without env") {
  const auto records = SettingsRegistryRecords();
  const auto* record = FindRecord(records, "provider.model_settings.effort");
  REQUIRE(record != nullptr);
  REQUIRE(record->toml_path == "provider.model_settings.effort");
  REQUIRE(record->env_var.empty());
  REQUIRE(record->value_type == yac::chat::SettingValueType::String);
  REQUIRE(record->classification == SettingClassification::External);
  REQUIRE(record->docs.settings_example);
  REQUIRE(record->docs.default_template);
  REQUIRE(record->validation.allowed_values.size() == 6);
}

TEST_CASE("settings template ordering follows registry order") {
  const auto& generated = yac::chat::kDefaultSettingsToml;
  const auto temperature_pos = generated.find("temperature = ");
  const auto provider_pos = generated.find("id          = ");
  const auto lsp_pos = generated.find("command = ");
  const auto theme_pos = generated.find("name = ");
  const auto compact_pos = generated.find("auto_enabled = ");
  const auto mcp_pos = generated.find("# result_max_bytes = ");

  REQUIRE(temperature_pos != std::string::npos);
  REQUIRE(provider_pos != std::string::npos);
  REQUIRE(lsp_pos != std::string::npos);
  REQUIRE(theme_pos != std::string::npos);
  REQUIRE(compact_pos != std::string::npos);
  REQUIRE(mcp_pos != std::string::npos);
  REQUIRE(temperature_pos < provider_pos);
  REQUIRE(provider_pos < lsp_pos);
  REQUIRE(lsp_pos < theme_pos);
  REQUIRE(theme_pos < compact_pos);
  REQUIRE(compact_pos < mcp_pos);
}

TEST_CASE("OpenAI auth settings templates describe device login") {
  constexpr std::string_view kStaleUnsupportedOpenAiAuth =
      "Unsupported for OpenAI auth: device-code, headless, and non-browser "
      "OAuth flows.";
  const std::string settings_example =
      ReadFile(YAC_STRINGIFY(SETTINGS_EXAMPLE_TOML_PATH));

  REQUIRE(yac::chat::kDefaultSettingsToml.find(kStaleUnsupportedOpenAiAuth) ==
          std::string::npos);
  REQUIRE(yac::chat::kDefaultSettingsToml.find(
              "yac auth openai login --device") != std::string::npos);
  REQUIRE(settings_example.find(kStaleUnsupportedOpenAiAuth) ==
          std::string::npos);
  REQUIRE(settings_example.find("yac auth openai login --device") !=
          std::string::npos);
}

TEST_CASE("MCP docs match registry-documented settings") {
  const auto tokens = ExtractDocTokens(ReadFile(YAC_STRINGIFY(MCP_DOCS_PATH)));
  const auto issues = ValidateDocTokens(
      tokens, SettingsRegistryRecords(), DynamicSettingsRegistryPatterns(),
      &yac::chat::SettingsDocExpectation::mcp_docs);

  RequireNoDocIssues(issues);
}

TEST_CASE("MCP docs do not advertise unsupported CLI subcommands") {
  const std::string docs = ReadFile(YAC_STRINGIFY(MCP_DOCS_PATH));
  const std::string cli = ReadFile("src/cli/mcp_cli_dispatch.cpp");
  constexpr std::array unsupported_commands = {"remove", "resources", "read"};

  for (const std::string_view command : unsupported_commands) {
    INFO(command);
    const std::string advertised = "yac mcp " + std::string(command);
    const std::string implemented =
        "subcmd == \"" + std::string(command) + "\"";
    REQUIRE((docs.find(advertised) == std::string::npos ||
             cli.find(implemented) != std::string::npos));
  }
}

TEST_CASE("OpenAI auth docs match registry-documented settings") {
  const auto tokens =
      ExtractDocTokens(ReadFile(YAC_STRINGIFY(OPENAI_AUTH_DOCS_PATH)));
  const auto issues = ValidateDocTokens(
      tokens, SettingsRegistryRecords(), DynamicSettingsRegistryPatterns(),
      &yac::chat::SettingsDocExpectation::openai_auth_docs);

  RequireNoDocIssues(issues);
}

TEST_CASE("OpenAI auth and Plan docs describe Task 11 behavior") {
  const std::string readme = ReadFile(YAC_STRINGIFY(README_MD_PATH));
  const std::string openai_docs =
      ReadFile(YAC_STRINGIFY(OPENAI_AUTH_DOCS_PATH));
  const std::string combined = readme + openai_docs;

  for (const auto* text : {&readme, &openai_docs}) {
    REQUIRE(text->find("http://localhost:1455/auth/callback") !=
            std::string::npos);
    REQUIRE(text->find("yac auth openai login --device") != std::string::npos);
    REQUIRE(text->find(".opencode/plans/") != std::string::npos);
  }
  REQUIRE(combined.find("Unsupported for OpenAI auth: device-code, headless, "
                        "and non-browser OAuth flows.") == std::string::npos);
  REQUIRE(combined.find("plan_exit") != std::string::npos);
}

TEST_CASE("provider preset prose stays within registered docs tokens") {
  const auto readme_tokens =
      ExtractDocTokens(ReadFile(YAC_STRINGIFY(README_MD_PATH)));
  REQUIRE(readme_tokens.toml_paths.contains("provider.id"));
  REQUIRE(readme_tokens.toml_paths.contains("provider.model"));
  REQUIRE(readme_tokens.toml_paths.contains("provider.base_url"));
  REQUIRE(readme_tokens.toml_paths.contains("provider.api_key_env"));
  REQUIRE(readme_tokens.toml_paths.contains("provider.options.region"));
  REQUIRE(readme_tokens.toml_paths.contains("provider.options.max_tokens"));
  REQUIRE(readme_tokens.env_vars.contains("YAC_PROVIDER"));
  REQUIRE(readme_tokens.env_vars.contains("YAC_MODEL"));
  REQUIRE(readme_tokens.env_vars.contains("YAC_BASE_URL"));
  REQUIRE(readme_tokens.env_vars.contains("YAC_API_KEY_ENV"));
  REQUIRE(readme_tokens.env_vars.contains("YAC_BEDROCK_REGION"));
  REQUIRE(readme_tokens.env_vars.contains("YAC_BEDROCK_MAX_TOKENS"));
  REQUIRE(readme_tokens.env_vars.contains("OPENAI_API_KEY"));
  REQUIRE(readme_tokens.env_vars.contains("ZAI_API_KEY"));

  const auto readme_issues =
      ValidateDocTokens(readme_tokens, SettingsRegistryRecords(),
                        DynamicSettingsRegistryPatterns(),
                        &yac::chat::SettingsDocExpectation::readme);
  RequireNoDocIssues(readme_issues);

  const auto openai_tokens =
      ExtractDocTokens(ReadFile(YAC_STRINGIFY(OPENAI_AUTH_DOCS_PATH)));
  REQUIRE(openai_tokens.env_vars.contains("OPENAI_API_KEY"));
  const auto openai_issues =
      ValidateDocTokens(openai_tokens, SettingsRegistryRecords(),
                        DynamicSettingsRegistryPatterns(),
                        &yac::chat::SettingsDocExpectation::openai_auth_docs);
  RequireNoDocIssues(openai_issues);
}

TEST_CASE("doc validation rejects fake YAC env vars") {
  DocTokens tokens;
  tokens.env_vars.insert("YAC_NOT_REGISTERED_FOR_DOCS");

  const auto issues = ValidateDocTokens(
      tokens, SettingsRegistryRecords(), DynamicSettingsRegistryPatterns(),
      &yac::chat::SettingsDocExpectation::readme);

  REQUIRE(HasDocIssue(issues,
                      "Unregistered env var: "
                      "YAC_NOT_REGISTERED_FOR_DOCS"));
}

TEST_CASE("doc validation rejects unsupported provider options examples") {
  DocTokens tokens;
  tokens.toml_paths.insert("provider.options.not_supported");

  const auto issues = ValidateDocTokens(
      tokens, SettingsRegistryRecords(), DynamicSettingsRegistryPatterns(),
      &yac::chat::SettingsDocExpectation::settings_example);

  REQUIRE(HasDocIssue(issues,
                      "Unregistered TOML key: "
                      "provider.options.not_supported"));
}

TEST_CASE("settings registry represents core scalar TOML env parity") {
  struct ExpectedRecord {
    std::string_view key;
    std::string_view toml_path;
    std::string_view env_var;
    yac::chat::SettingValueType value_type;
  };

  constexpr std::array expected = {
      ExpectedRecord{"temperature", "temperature", "YAC_TEMPERATURE",
                     yac::chat::SettingValueType::Number},
      ExpectedRecord{"system_prompt", "system_prompt", "YAC_SYSTEM_PROMPT",
                     yac::chat::SettingValueType::String},
      ExpectedRecord{"workspace_root", "workspace_root", "YAC_WORKSPACE_ROOT",
                     yac::chat::SettingValueType::String},
      ExpectedRecord{"provider.context_window", "provider.context_window",
                     "YAC_CONTEXT_WINDOW",
                     yac::chat::SettingValueType::Integer},
      ExpectedRecord{"lsp.clangd.command", "lsp.clangd.command",
                     "YAC_LSP_CLANGD_COMMAND",
                     yac::chat::SettingValueType::String},
      ExpectedRecord{"lsp.clangd.args", "lsp.clangd.args",
                     "YAC_LSP_CLANGD_ARGS",
                     yac::chat::SettingValueType::StringArray},
      ExpectedRecord{
          "theme.sync_terminal_background", "theme.sync_terminal_background",
          "YAC_SYNC_TERMINAL_BACKGROUND", yac::chat::SettingValueType::Bool},
      ExpectedRecord{"theme.name", "theme.name", "YAC_THEME_NAME",
                     yac::chat::SettingValueType::String},
      ExpectedRecord{"theme.density", "theme.density", "YAC_THEME_DENSITY",
                     yac::chat::SettingValueType::String},
      ExpectedRecord{"compact.auto_enabled", "compact.auto_enabled",
                     "YAC_COMPACT_AUTO_ENABLED",
                     yac::chat::SettingValueType::Bool},
      ExpectedRecord{"compact.threshold", "compact.threshold",
                     "YAC_COMPACT_THRESHOLD",
                     yac::chat::SettingValueType::Number},
      ExpectedRecord{"compact.keep_last", "compact.keep_last",
                     "YAC_COMPACT_KEEP_LAST",
                     yac::chat::SettingValueType::Integer},
      ExpectedRecord{"compact.mode", "compact.mode", "YAC_COMPACT_MODE",
                     yac::chat::SettingValueType::String},
      ExpectedRecord{"web_search.enabled", "web_search.enabled",
                     "YAC_WEB_SEARCH_ENABLED",
                     yac::chat::SettingValueType::Bool},
      ExpectedRecord{"web_search.provider", "web_search.provider",
                     "YAC_WEB_SEARCH_PROVIDER",
                     yac::chat::SettingValueType::String},
      ExpectedRecord{"web_search.endpoint", "web_search.endpoint",
                     "YAC_EXA_ENDPOINT", yac::chat::SettingValueType::String},
      ExpectedRecord{"web_search.timeout_seconds", "web_search.timeout_seconds",
                     "YAC_WEB_SEARCH_TIMEOUT_SECONDS",
                     yac::chat::SettingValueType::Integer},
  };

  const auto records = SettingsRegistryRecords();
  for (const auto& expected_record : expected) {
    INFO(expected_record.key);
    REQUIRE(std::ranges::any_of(records, [&](const SettingMetadata& record) {
      return record.key == expected_record.key &&
             record.toml_path == expected_record.toml_path &&
             record.env_var == expected_record.env_var &&
             record.value_type == expected_record.value_type &&
             record.classification == SettingClassification::UserFacing;
    }));
  }
}

TEST_CASE("settings registry represents Bedrock provider option parity") {
  struct ExpectedRecord {
    std::string_view key;
    std::string_view toml_path;
    std::string_view env_var;
    yac::chat::SettingValueType value_type;
  };

  constexpr std::array expected = {
      ExpectedRecord{"provider.options.region", "provider.options.region",
                     "YAC_BEDROCK_REGION", yac::chat::SettingValueType::String},
      ExpectedRecord{"provider.options.max_tokens",
                     "provider.options.max_tokens", "YAC_BEDROCK_MAX_TOKENS",
                     yac::chat::SettingValueType::Integer},
      ExpectedRecord{"provider.options.profile", "provider.options.profile",
                     "YAC_BEDROCK_PROFILE",
                     yac::chat::SettingValueType::String},
      ExpectedRecord{"provider.options.endpoint_override",
                     "provider.options.endpoint_override",
                     "YAC_BEDROCK_ENDPOINT_OVERRIDE",
                     yac::chat::SettingValueType::String},
      ExpectedRecord{"provider.options.credential_refresh_command",
                     "provider.options.credential_refresh_command",
                     "YAC_BEDROCK_CREDENTIAL_REFRESH_COMMAND",
                     yac::chat::SettingValueType::String},
  };

  const auto records = SettingsRegistryRecords();
  for (const auto& expected_record : expected) {
    INFO(expected_record.key);
    REQUIRE(std::ranges::any_of(records, [&](const SettingMetadata& record) {
      return record.key == expected_record.key &&
             record.toml_path == expected_record.toml_path &&
             record.env_var == expected_record.env_var &&
             record.value_type == expected_record.value_type &&
             record.classification == SettingClassification::UserFacing;
    }));
  }

  REQUIRE(std::ranges::any_of(records, [](const SettingMetadata& record) {
    return record.key == "bedrock.aws_region_fallback" &&
           record.env_var == "AWS_REGION" && record.toml_path.empty() &&
           record.classification == SettingClassification::External;
  }));
}

TEST_CASE(
    "settings registry classifies env-only secret operational and external "
    "records") {
  struct ExpectedRecord {
    std::string_view key;
    std::string_view env_var;
    yac::chat::SettingValueType value_type;
    SettingClassification classification;
  };

  constexpr std::array expected = {
      ExpectedRecord{"openai.api_key_env_value", "OPENAI_API_KEY",
                     yac::chat::SettingValueType::Secret,
                     SettingClassification::Secret},
      ExpectedRecord{"zai.api_key_env_value", "ZAI_API_KEY",
                     yac::chat::SettingValueType::Secret,
                     SettingClassification::Secret},
      ExpectedRecord{"web_search.exa_api_key_env_value", "YAC_EXA_API_KEY",
                     yac::chat::SettingValueType::Secret,
                     SettingClassification::Secret},
      ExpectedRecord{"openai.auth_store", "YAC_OPENAI_AUTH_STORE",
                     yac::chat::SettingValueType::String,
                     SettingClassification::Operational},
      ExpectedRecord{"mcp.token_store", "YAC_MCP_TOKEN_STORE",
                     yac::chat::SettingValueType::String,
                     SettingClassification::Operational},
      ExpectedRecord{"keychain.disabled", "YAC_KEYCHAIN_DISABLED",
                     yac::chat::SettingValueType::Bool,
                     SettingClassification::Operational},
      ExpectedRecord{"bedrock.aws_region_fallback", "AWS_REGION",
                     yac::chat::SettingValueType::String,
                     SettingClassification::External},
      ExpectedRecord{"process.home_dir_fallback", "HOME",
                     yac::chat::SettingValueType::String,
                     SettingClassification::External},
      ExpectedRecord{"process.path_lookup", "PATH",
                     yac::chat::SettingValueType::String,
                     SettingClassification::External},
  };

  const auto records = SettingsRegistryRecords();
  std::vector<SettingMetadata> env_only_records;
  env_only_records.reserve(expected.size());

  for (const auto& expected_record : expected) {
    INFO(expected_record.key);
    const auto* record = FindRecord(records, expected_record.key);
    REQUIRE(record != nullptr);
    REQUIRE(record->env_var == expected_record.env_var);
    REQUIRE(record->toml_path.empty());
    REQUIRE(record->value_type == expected_record.value_type);
    REQUIRE(record->classification == expected_record.classification);
    if (record->env_var == "OPENAI_API_KEY" ||
        record->env_var == "ZAI_API_KEY") {
      REQUIRE(record->docs.configuration_docs);
      REQUIRE(record->toml_path.empty());
    }
    env_only_records.push_back(*record);
  }

  const auto issues = ValidateSettingsRegistry(env_only_records, {});
  REQUIRE(issues.empty());
}

TEST_CASE(
    "settings registry user-facing records require TOML and env metadata") {
  auto records = MutableRecords();
  records.push_back({.key = "test.user_without_env",
                     .toml_path = "test.user_without_env",
                     .classification = SettingClassification::UserFacing});

  const auto issues =
      ValidateSettingsRegistry(records, DynamicSettingsRegistryPatterns());

  REQUIRE(
      HasIssue(issues, "User-facing setting requires TOML and env metadata"));
  REQUIRE(HasIssue(issues, "test.user_without_env"));
}

TEST_CASE("settings registry env-only non-user-facing records are valid") {
  constexpr std::array records = {
      SettingMetadata{.key = "test.secret",
                      .env_var = "YAC_TEST_SECRET",
                      .classification = SettingClassification::Secret},
      SettingMetadata{.key = "test.operational",
                      .env_var = "YAC_TEST_OPERATIONAL",
                      .classification = SettingClassification::Operational},
      SettingMetadata{.key = "test.external",
                      .env_var = "YAC_TEST_EXTERNAL",
                      .classification = SettingClassification::External},
  };

  const auto issues = ValidateSettingsRegistry(records, {});

  REQUIRE(issues.empty());
}

TEST_CASE("settings registry rejects duplicate static keys") {
  auto records = MutableRecords();
  records.push_back(records.front());

  const auto issues =
      ValidateSettingsRegistry(records, DynamicSettingsRegistryPatterns());

  REQUIRE(HasIssue(issues, "Duplicate static key"));
  REQUIRE(HasIssue(issues, std::string{records.front().key}));
}

TEST_CASE("settings registry rejects duplicate TOML paths") {
  auto records = MutableRecords();
  records.push_back({.key = "test.duplicate_toml",
                     .toml_path = records.front().toml_path,
                     .env_var = "YAC_TEST_DUPLICATE_TOML",
                     .classification = SettingClassification::Operational});

  const auto issues =
      ValidateSettingsRegistry(records, DynamicSettingsRegistryPatterns());

  REQUIRE(HasIssue(issues, "Duplicate TOML path"));
  REQUIRE(HasIssue(issues, std::string{records.front().toml_path}));
}

TEST_CASE("settings registry rejects duplicate static env vars") {
  auto records = MutableRecords();
  records.push_back({.key = "test.duplicate_env",
                     .env_var = records.front().env_var,
                     .classification = SettingClassification::Operational});

  const auto issues =
      ValidateSettingsRegistry(records, DynamicSettingsRegistryPatterns());

  REQUIRE(HasIssue(issues, "Duplicate env var"));
  REQUIRE(HasIssue(issues, std::string{records.front().env_var}));
}

TEST_CASE("settings registry represents dynamic MCP patterns separately") {
  const auto records = SettingsRegistryRecords();
  const auto patterns = DynamicSettingsRegistryPatterns();

  REQUIRE_FALSE(patterns.empty());
  REQUIRE(std::ranges::none_of(records, [](const SettingMetadata& record) {
    return record.env_var.find("YAC_MCP_<ID>_") != std::string_view::npos;
  }));
  REQUIRE(
      std::ranges::any_of(patterns, [](const DynamicSettingPattern& pattern) {
        return pattern.key == "mcp.servers.command" &&
               pattern.toml_path_pattern == "mcp.servers[].command" &&
               pattern.env_var_pattern == "YAC_MCP_<ID>_COMMAND";
      }));

  auto duplicate_patterns = MutableDynamicPatterns();
  duplicate_patterns.push_back(duplicate_patterns[1]);
  const auto issues = ValidateSettingsRegistry(records, duplicate_patterns);
  REQUIRE(HasIssue(issues, "Duplicate dynamic key"));
  REQUIRE(HasIssue(issues, "Duplicate dynamic env pattern"));
}

TEST_CASE("settings registry represents MCP global and dynamic parity") {
  const auto records = SettingsRegistryRecords();
  REQUIRE(std::ranges::any_of(records, [](const SettingMetadata& record) {
    return record.key == "mcp.result_max_bytes" &&
           record.toml_path == "mcp.result_max_bytes" &&
           record.env_var == "YAC_MCP_RESULT_MAX_BYTES" &&
           record.value_type == yac::chat::SettingValueType::Integer &&
           record.classification == SettingClassification::UserFacing;
  }));

  struct ExpectedPattern {
    std::string_view key;
    std::string_view toml_path_pattern;
    std::string_view env_var_pattern;
    yac::chat::SettingValueType value_type;
  };

  constexpr std::array expected = {
      ExpectedPattern{"mcp.servers.transport", "mcp.servers[].transport",
                      "YAC_MCP_<ID>_TRANSPORT",
                      yac::chat::SettingValueType::String},
      ExpectedPattern{"mcp.servers.command", "mcp.servers[].command",
                      "YAC_MCP_<ID>_COMMAND",
                      yac::chat::SettingValueType::String},
      ExpectedPattern{"mcp.servers.args", "mcp.servers[].args",
                      "YAC_MCP_<ID>_ARGS",
                      yac::chat::SettingValueType::StringArray},
      ExpectedPattern{"mcp.servers.url", "mcp.servers[].url",
                      "YAC_MCP_<ID>_URL", yac::chat::SettingValueType::String},
      ExpectedPattern{"mcp.servers.enabled", "mcp.servers[].enabled",
                      "YAC_MCP_<ID>_ENABLED",
                      yac::chat::SettingValueType::Bool},
      ExpectedPattern{"mcp.servers.auto_start", "mcp.servers[].auto_start",
                      "YAC_MCP_<ID>_AUTO_START",
                      yac::chat::SettingValueType::Bool},
      ExpectedPattern{
          "mcp.servers.requires_approval", "mcp.servers[].requires_approval",
          "YAC_MCP_<ID>_REQUIRES_APPROVAL", yac::chat::SettingValueType::Bool},
      ExpectedPattern{"mcp.servers.approval_required_tools",
                      "mcp.servers[].approval_required_tools",
                      "YAC_MCP_<ID>_APPROVAL_REQUIRED_TOOLS",
                      yac::chat::SettingValueType::StringArray},
      ExpectedPattern{
          "mcp.servers.auth.api_key_env", "mcp.servers[].auth.api_key_env",
          "YAC_MCP_<ID>_API_KEY_ENV", yac::chat::SettingValueType::String},
      ExpectedPattern{"mcp.servers.env_json", "mcp.servers[].env",
                      "YAC_MCP_<ID>_ENV_JSON",
                      yac::chat::SettingValueType::String},
      ExpectedPattern{"mcp.servers.headers_json", "mcp.servers[].headers",
                      "YAC_MCP_<ID>_HEADERS_JSON",
                      yac::chat::SettingValueType::String},
      ExpectedPattern{"mcp.servers.auth.type", "mcp.servers[].auth.type",
                      "YAC_MCP_<ID>_AUTH_TYPE",
                      yac::chat::SettingValueType::String},
      ExpectedPattern{"mcp.servers.auth.oauth.authorization_url",
                      "mcp.servers[].auth.authorization_url",
                      "YAC_MCP_<ID>_OAUTH_AUTHORIZATION_URL",
                      yac::chat::SettingValueType::String},
      ExpectedPattern{
          "mcp.servers.auth.oauth.token_url", "mcp.servers[].auth.token_url",
          "YAC_MCP_<ID>_OAUTH_TOKEN_URL", yac::chat::SettingValueType::String},
      ExpectedPattern{
          "mcp.servers.auth.oauth.client_id", "mcp.servers[].auth.client_id",
          "YAC_MCP_<ID>_OAUTH_CLIENT_ID", yac::chat::SettingValueType::String},
      ExpectedPattern{"mcp.servers.auth.oauth.scopes",
                      "mcp.servers[].auth.scopes", "YAC_MCP_<ID>_OAUTH_SCOPES",
                      yac::chat::SettingValueType::StringArray},
  };

  const auto patterns = DynamicSettingsRegistryPatterns();
  for (const auto& expected_pattern : expected) {
    INFO(expected_pattern.key);
    REQUIRE(std::ranges::any_of(
        patterns, [&](const DynamicSettingPattern& pattern) {
          return pattern.key == expected_pattern.key &&
                 pattern.toml_path_pattern ==
                     expected_pattern.toml_path_pattern &&
                 pattern.env_var_pattern == expected_pattern.env_var_pattern &&
                 pattern.value_type == expected_pattern.value_type &&
                 pattern.classification == SettingClassification::External;
        }));
  }
}
