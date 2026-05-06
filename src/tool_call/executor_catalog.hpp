#pragma once

#include "core_types/chat_ids.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/executor_arguments.hpp"

#include <cstddef>
#include <string_view>
#include <vector>

namespace yac::tool_call {

[[nodiscard]] std::vector<chat::ToolDefinition> ToolDefinitions();
[[nodiscard]] PreparedToolCall PrepareToolCall(
    const chat::ToolCallRequest& request);

// Internal-but-exposed (for ToolExecutor::Execute and tests). One handler per
// built-in tool; both halves are registered together so adding a tool requires
// updating exactly one site.
struct ToolHandler {
  using PrepareFn = PreparedToolCall (*)(const chat::ToolCallRequest& request,
                                         const Json& args);
  using ExecuteFn = ToolExecutionResult (*)(const PreparedToolCall& prepared,
                                            const ExecutionContext& ctx);
  PrepareFn prepare;
  ExecuteFn execute;
};

[[nodiscard]] const ToolHandler* LookupToolHandler(std::string_view name);
[[nodiscard]] std::size_t ToolHandlerCount();

}  // namespace yac::tool_call
