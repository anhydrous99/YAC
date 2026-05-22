#pragma once

#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace yac::chat {

enum class SettingValueType {
  String,
  StringArray,
  Bool,
  Integer,
  Number,
  Secret,
};

enum class SettingClassification {
  UserFacing,
  Secret,
  Operational,
  External,
  Internal,
  Deprecated,
};

struct SettingsDocExpectation {
  bool readme = false;
  bool settings_example = false;
  bool default_template = false;
  bool mcp_docs = false;
  bool openai_auth_docs = false;
};

struct SettingMetadata {
  std::string_view key;
  std::string_view toml_path;
  std::string_view env_var;
  SettingValueType value_type = SettingValueType::String;
  std::string_view default_description;
  SettingClassification classification = SettingClassification::Internal;
  SettingsDocExpectation docs;
};

struct DynamicSettingPattern {
  std::string_view key;
  std::string_view toml_path_pattern;
  std::string_view env_var_pattern;
  SettingValueType value_type = SettingValueType::String;
  std::string_view default_description;
  SettingClassification classification = SettingClassification::External;
  SettingsDocExpectation docs;
};

struct SettingsRegistryIssue {
  std::string message;
};

[[nodiscard]] std::span<const SettingClassification> SettingClassifications();
[[nodiscard]] std::span<const SettingMetadata> SettingsRegistryRecords();
[[nodiscard]] std::span<const DynamicSettingPattern>
DynamicSettingsRegistryPatterns();

[[nodiscard]] std::vector<SettingsRegistryIssue> ValidateSettingsRegistry(
    std::span<const SettingMetadata> records,
    std::span<const DynamicSettingPattern> dynamic_patterns);

}  // namespace yac::chat
