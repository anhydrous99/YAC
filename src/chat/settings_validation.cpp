#include "chat/settings_validation.hpp"

#include <cstddef>
#include <sstream>
#include <string>
#include <string_view>

namespace yac::chat {

namespace {

std::string FormatValidationNumber(double value) {
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

std::string ValidationDetail(const SettingValidationMetadata& validation,
                             std::string_view prefix, std::string_view suffix) {
  switch (validation.type) {
    case SettingValidationType::NumberRange:
      return std::string(prefix) + "must be between " +
             FormatValidationNumber(validation.min_number) + " and " +
             FormatValidationNumber(validation.max_number) +
             std::string(suffix);
    case SettingValidationType::IntegerRange:
      return std::string(prefix) + "must be between " +
             std::to_string(validation.min_integer) + " and " +
             std::to_string(validation.max_integer) + std::string(suffix);
    case SettingValidationType::PositiveInteger:
      return std::string(prefix) + "must be a positive integer" +
             std::string(suffix);
    case SettingValidationType::StringEnum:
      return std::string(prefix) + "must be " +
             JoinAllowedValues(validation.allowed_values) + std::string(suffix);
    case SettingValidationType::None:
    case SettingValidationType::Required:
      return {};
  }
  return {};
}

}  // namespace

std::string TomlValidationDetail(const SettingValidationMetadata& validation) {
  return ValidationDetail(validation, "Value ", ".");
}

std::string EnvValidationDetail(const SettingValidationMetadata& validation) {
  return ValidationDetail(validation, "", "");
}

}  // namespace yac::chat
