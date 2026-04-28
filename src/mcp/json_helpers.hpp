#pragma once

#include "mcp/protocol_messages.hpp"

#include <exception>
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

}  // namespace yac::mcp
