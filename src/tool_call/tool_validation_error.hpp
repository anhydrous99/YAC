#pragma once

#include "core_types/chat_ids.hpp"

#include <openai.hpp>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace yac::tool_call {

using ValidationJson = openai::_detail::Json;

class ToolValidationError : public std::runtime_error {
 public:
  ToolValidationError(std::string message, std::string tool_name,
                      std::string raw_arguments_json)
      : std::runtime_error(message),
        tool_name_(std::move(tool_name)),
        raw_arguments_json_(std::move(raw_arguments_json)) {}

  [[nodiscard]] const std::string& tool_name() const noexcept {
    return tool_name_;
  }
  [[nodiscard]] const std::string& raw_arguments_json() const noexcept {
    return raw_arguments_json_;
  }

 private:
  std::string tool_name_;
  std::string raw_arguments_json_;
};

namespace detail {

inline ValidationJson BuildValidationErrorPayload(
    const std::string& message, const std::string& tool_name,
    const std::string& raw_arguments_json,
    const std::vector<chat::ToolDefinition>* definitions) {
  ValidationJson payload = ValidationJson::object();
  payload["error"] = message;
  if (!tool_name.empty()) {
    payload["tool_name"] = tool_name;
  }
  payload["received_arguments"] = raw_arguments_json;
  if (definitions != nullptr && !tool_name.empty()) {
    for (const auto& def : *definitions) {
      if (def.name != tool_name) {
        continue;
      }
      try {
        payload["expected_schema"] =
            ValidationJson::parse(def.parameters_schema_json);
      } catch (const std::exception& parse_error) {
        payload["expected_schema"] = def.parameters_schema_json;
        payload["expected_schema_parse_error"] = parse_error.what();
      }
      break;
    }
  }
  return payload;
}

}  // namespace detail

[[nodiscard]] inline std::string BuildValidationErrorJson(
    const ToolValidationError& error,
    const std::vector<chat::ToolDefinition>& definitions) {
  return detail::BuildValidationErrorPayload(error.what(), error.tool_name(),
                                             error.raw_arguments_json(),
                                             &definitions)
      .dump();
}

[[nodiscard]] inline std::string BuildValidationErrorJson(
    const std::string& message, const std::string& tool_name,
    const std::string& raw_arguments_json) {
  return detail::BuildValidationErrorPayload(message, tool_name,
                                             raw_arguments_json, nullptr)
      .dump();
}

}  // namespace yac::tool_call
