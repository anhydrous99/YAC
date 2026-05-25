#pragma once

#include "chat/chat_service_prompt_processor.hpp"

#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

namespace yac::chat::internal {

class ToolRoundRunner {
 public:
  using PromptRunContext = ChatServicePromptProcessor::PromptRunContext;

  explicit ToolRoundRunner(PromptRunContext context);

  void Run(
      const std::vector<ToolCallRequest>& requested_tools,
      const std::unordered_map<ToolCallId, ChatMessageId>& streaming_card_ids,
      uint64_t generation, std::stop_token stop_token);

 private:
  struct ToolPrepFailure {
    std::string error;
    std::string result_json;
  };
  struct ToolPrep {
    ::yac::tool_call::PreparedToolCall prepared;
    std::optional<ToolPrepFailure> failure;
  };

  [[nodiscard]] ToolPrep PrepareOneToolCall(const ToolCallRequest& request,
                                            bool is_mcp_tool) const;
  [[nodiscard]] bool MaybeAwaitApproval(
      ::yac::tool_call::PreparedToolCall& prepared,
      const ToolCallRequest& tool_request, ChatMessageId tool_message_id,
      std::stop_token stop_token, bool& gate_aborted);
  [[nodiscard]] ::yac::tool_call::ToolExecutionResult ExecuteOneToolCall(
      const ::yac::tool_call::PreparedToolCall& prepared,
      const std::optional<ToolPrepFailure>& failure, bool approved,
      bool is_mcp_tool, std::stop_token stop_token);
  [[nodiscard]] bool ShouldAbortLocked(uint64_t generation) const;
  [[nodiscard]] static ::yac::tool_call::ToolExecutionResult
  MakeRejectedToolResult(const ::yac::tool_call::PreparedToolCall& prepared);

  ToolApprovalManager* tool_approval_;
  ChatServiceMcp* chat_service_mcp_;
  std::mutex* history_mutex_;
  std::vector<ChatMessage>* history_;
  ChatServicePromptProcessor::EmitEventFn emit_event_;
  ChatServicePromptProcessor::NextMessageIdFn next_message_id_;
  ChatServicePromptProcessor::ConfigSnapshotFn config_snapshot_;
  ChatServicePromptProcessor::GenerationValueFn generation_value_;
  std::mutex* approval_gate_;
  ChatServicePromptProcessor::PrepareBuiltInToolCallFn
      prepare_built_in_tool_call_;
  ChatServicePromptProcessor::ExecuteBuiltInToolCallFn
      execute_built_in_tool_call_;
  ChatServicePromptProcessor::OnPlanExitApprovedFn on_plan_exit_approved_;
};

}  // namespace yac::chat::internal
