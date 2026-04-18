#include "tool_call/sub_agent_tool_executor.hpp"

#include "chat/sub_agent_manager.hpp"
#include "core_types/tool_call_types.hpp"
#include "tool_call/executor_arguments.hpp"

#include <utility>
#include <variant>

namespace yac::tool_call {

namespace {

[[nodiscard]] ToolExecutionResult ErrorResult(ToolCallBlock block,
                                              const std::string& message) {
  std::visit(
      [&message](auto& call) {
        if constexpr (requires {
                        call.is_error;
                        call.error;
                      }) {
          call.is_error = true;
          call.error = message;
        }
      },
      block);
  return ToolExecutionResult{
      .block = std::move(block),
      .result_json = Json{{"error", message}}.dump(),
      .is_error = true,
  };
}

}  // namespace

ToolExecutionResult ExecuteSubAgentTool(
    const PreparedToolCall& prepared,
    chat::SubAgentManager* sub_agent_manager) {
  if (sub_agent_manager == nullptr) {
    return ErrorResult(prepared.preview, "Sub-agent manager not available.");
  }

  const auto* call = std::get_if<SubAgentCall>(&prepared.preview);
  if (call == nullptr) {
    return ErrorResult(prepared.preview, "Invalid sub_agent tool call.");
  }

  if (sub_agent_manager->IsAtCapacity()) {
    const std::string capacity_result =
        "Maximum concurrent sub-agents (" +
        std::to_string(chat::kMaxConcurrentSubAgents) + ") reached.";
    const std::string capacity_error =
        capacity_result + " Wait for existing agents to complete.";
    SubAgentCall block{.task = call->task,
                       .mode = call->mode,
                       .status = SubAgentStatus::Error,
                       .result = capacity_result};
    return ToolExecutionResult{
        .block = std::move(block),
        .result_json = Json{{"error", capacity_error}}.dump(),
        .is_error = true};
  }

  if (call->mode == SubAgentMode::Background) {
    const auto agent_id = sub_agent_manager->SpawnBackground(call->task);
    auto block = SubAgentCall{.task = call->task,
                              .mode = SubAgentMode::Background,
                              .status = SubAgentStatus::Running,
                              .agent_id = agent_id};
    return ToolExecutionResult{
        .block = std::move(block),
        .result_json = Json{{"status", "spawned"},
                            {"agent_id", agent_id},
                            {"message",
                             "Sub-agent spawned in background. "
                             "Results will appear when complete."}}
                           .dump()};
  }

  const auto result = sub_agent_manager->SpawnForeground(call->task);
  auto block = SubAgentCall{.task = call->task,
                            .mode = SubAgentMode::Foreground,
                            .status = SubAgentStatus::Complete,
                            .result = result};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = Json{{"result", result}}.dump()};
}

}  // namespace yac::tool_call
