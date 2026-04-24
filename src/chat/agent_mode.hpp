#pragma once

#include <set>
#include <string>
#include <string_view>

namespace yac::chat {

enum class AgentMode { Build, Plan };

[[nodiscard]] std::set<std::string> ExcludedToolsForMode(AgentMode mode);

[[nodiscard]] std::string_view AgentModeLabel(AgentMode mode);

}  // namespace yac::chat
