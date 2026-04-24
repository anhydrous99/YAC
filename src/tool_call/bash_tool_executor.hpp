#pragma once

#include "tool_call/executor.hpp"

#include <filesystem>
#include <stop_token>

namespace yac::tool_call {

[[nodiscard]] ToolExecutionResult ExecuteBashTool(
    const chat::ToolCallRequest& request,
    const std::filesystem::path& workspace_root, std::stop_token stop_token);

}  // namespace yac::tool_call
