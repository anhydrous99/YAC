#pragma once

#include "mcp/protocol_messages.hpp"

#include <exception>
#include <optional>
#include <string>
#include <string_view>

namespace yac::mcp {

[[nodiscard]] inline Json ParseJsonOrThrow(std::string_view body,
                                           std::string_view context) {
  try {
    return Json::parse(body);
  } catch (const std::exception& error) {
    std::string message;
    message.reserve(context.size() + 16 +
                    std::string_view(error.what()).size());
    message.append(context);
    message.append(": Invalid JSON: ");
    message.append(error.what());
    throw McpProtocolError(std::move(message));
  }
}

[[nodiscard]] inline std::string GetString(const Json& j,
                                           std::string_view field) {
  const std::string key{field};
  if (!j.contains(key) || !j[key].is_string()) {
    throw McpProtocolError("missing string field: " + key);
  }
  return j[key].get<std::string>();
}

[[nodiscard]] inline std::optional<std::string> GetOptString(
    const Json& j, std::string_view field) {
  const std::string key{field};
  if (!j.contains(key) || j[key].is_null()) {
    return std::nullopt;
  }
  if (!j[key].is_string()) {
    throw McpProtocolError("field not a string: " + key);
  }
  return j[key].get<std::string>();
}

[[nodiscard]] inline bool GetBool(const Json& j, std::string_view field,
                                  bool default_val = false) {
  const std::string key{field};
  if (!j.contains(key) || !j[key].is_boolean()) {
    return default_val;
  }
  return j[key].get<bool>();
}

[[nodiscard]] inline std::optional<double> GetOptDouble(
    const Json& j, std::string_view field) {
  const std::string key{field};
  if (!j.contains(key) || j[key].is_null()) {
    return std::nullopt;
  }
  if (!j[key].is_number()) {
    throw McpProtocolError("field not a number: " + key);
  }
  return j[key].get<double>();
}

}  // namespace yac::mcp
