#pragma once

#include "chat/types.hpp"

#include <openai.hpp>
#include <string>

namespace yac::tool_call {

using Json = openai::_detail::Json;

[[nodiscard]] Json ParseArguments(const chat::ToolCallRequest& request);
[[nodiscard]] std::string RequireString(const Json& args,
                                        const std::string& key);
[[nodiscard]] int RequireInt(const Json& args, const std::string& key);
[[nodiscard]] std::string OptionalString(const Json& args,
                                          const std::string& key);
[[nodiscard]] bool OptionalBool(const Json& args, const std::string& key,
                                bool default_value);

}  // namespace yac::tool_call
