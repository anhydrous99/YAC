#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace yac::mcp {

std::string SanitizeMcpToolName(std::string_view server_id,
                                std::string_view raw_tool_name);
bool IsMcpToolName(std::string_view name);
std::optional<std::pair<std::string, std::string>> SplitMcpToolName(
    std::string_view qualified);

}  // namespace yac::mcp
