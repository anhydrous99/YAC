#pragma once

#include "core_types/agent_mode.hpp"

#include <set>
#include <string>

namespace yac::chat {

[[nodiscard]] std::set<std::string> ExcludedToolsForMode(AgentMode mode);

}  // namespace yac::chat
