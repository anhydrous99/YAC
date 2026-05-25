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

enum class SettingRuntimeDefaultType {
  None,
  Unset,
  String,
  StringArray,
  Bool,
  Integer,
  Number,
};

enum class SettingValidationType {
  None,
  NumberRange,
  IntegerRange,
  StringEnum,
  PositiveInteger,
  Required,
};

struct SettingRuntimeDefault {
  SettingRuntimeDefaultType type = SettingRuntimeDefaultType::None;
  std::string_view string_value;
  bool bool_value = false;
  int integer_value = 0;
  double number_value = 0.0;
};

struct SettingValidationMetadata {
  SettingValidationType type = SettingValidationType::None;
  double min_number = 0.0;
  double max_number = 0.0;
  int min_integer = 0;
  int max_integer = 0;
  std::span<const std::string_view> allowed_values;
};

struct SettingsDocExpectation {
  bool readme = false;
  bool configuration_docs = false;
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
  SettingRuntimeDefault runtime_default;
  SettingValidationMetadata validation;
  SettingClassification classification = SettingClassification::Internal;
  SettingsDocExpectation docs;
};

struct DynamicSettingPattern {
  std::string_view key;
  std::string_view toml_path_pattern;
  std::string_view env_var_pattern;
  SettingValueType value_type = SettingValueType::String;
  std::string_view default_description;
  SettingRuntimeDefault runtime_default;
  SettingValidationMetadata validation;
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
[[nodiscard]] const SettingMetadata* FindSettingsRegistryRecord(
    std::string_view key);
[[nodiscard]] const DynamicSettingPattern* FindDynamicSettingsRegistryPattern(
    std::string_view key);

[[nodiscard]] std::vector<SettingsRegistryIssue> ValidateSettingsRegistry(
    std::span<const SettingMetadata> records,
    std::span<const DynamicSettingPattern> dynamic_patterns);

[[nodiscard]] std::string GenerateDefaultSettingsTomlTemplate();
[[nodiscard]] std::string GenerateSettingsExampleToml();

}  // namespace yac::chat
