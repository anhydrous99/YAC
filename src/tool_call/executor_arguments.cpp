#include "tool_call/executor_arguments.hpp"

#include <stdexcept>

namespace yac::tool_call {

Json ParseArguments(const chat::ToolCallRequest& request) {
  if (request.arguments_json.empty()) {
    return Json::object();
  }
  return Json::parse(request.arguments_json);
}

std::string RequireString(const Json& args, const std::string& key) {
  if (!args.contains(key) || !args[key].is_string()) {
    throw std::runtime_error("Missing string argument '" + key + "'.");
  }
  return args[key].get<std::string>();
}

int RequireInt(const Json& args, const std::string& key) {
  if (!args.contains(key) || !args[key].is_number_integer()) {
    throw std::runtime_error("Missing integer argument '" + key + "'.");
  }
  return args[key].get<int>();
}

std::string OptionalString(const Json& args, const std::string& key) {
  if (!args.contains(key) || !args[key].is_string()) {
    return {};
  }
  return args[key].get<std::string>();
}

bool OptionalBool(const Json& args, const std::string& key,
                  bool default_value) {
  if (!args.contains(key) || !args[key].is_boolean()) {
    return default_value;
  }
  return args[key].get<bool>();
}

}  // namespace yac::tool_call
