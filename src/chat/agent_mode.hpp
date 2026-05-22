#pragma once

#include "core_types/agent_mode.hpp"

#include <set>
#include <string>
#include <string_view>

namespace yac::chat {

[[nodiscard]] std::set<std::string> ExcludedToolsForMode(AgentMode mode);
[[nodiscard]] bool IsToolAllowedForMode(AgentMode mode,
                                        std::string_view tool_name);

}  // namespace yac::chat
