#pragma once

#include "chat/chat_service_request_builder.hpp"
#include "chat/chat_service_tool_approval.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"

#include <functional>
#include <mutex>
#include <set>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

namespace yac::chat::internal {

class ChatServiceMcp;

class ChatServicePromptProcessor {
 public:
  using EmitEventFn = std::function<void(ChatEvent)>;
  using NextMessageIdFn = std::function<ChatMessageId()>;
  using ConfigSnapshotFn = std::function<ChatConfig()>;
  using GenerationValueFn = std::function<uint64_t()>;
  using ModeExcludedToolsFn = std::function<std::set<std::string>()>;
  using PrepareBuiltInToolCallFn =
      std::function<::yac::tool_call::PreparedToolCall(const ToolCallRequest&)>;
  using ExecuteBuiltInToolCallFn =
      std::function<::yac::tool_call::ToolExecutionResult(
          const ::yac::tool_call::PreparedToolCall&, std::stop_token)>;

  ChatServicePromptProcessor(
      provider::ProviderRegistry& registry,
      ::yac::tool_call::ToolExecutor& tool_executor,
      ChatServiceToolApproval& tool_approval, ChatServiceMcp* chat_service_mcp,
      std::mutex& history_mutex, std::vector<ChatMessage>& history,
      EmitEventFn emit_event, NextMessageIdFn next_message_id,
      ConfigSnapshotFn config_snapshot, GenerationValueFn generation_value,
      std::set<std::string> excluded_tools = {},
      std::mutex* approval_gate = nullptr,
      ModeExcludedToolsFn mode_excluded_tools = {},
      PrepareBuiltInToolCallFn prepare_built_in_tool_call = {},
      ExecuteBuiltInToolCallFn execute_built_in_tool_call = {});

  void ProcessPrompt(ChatMessageId prompt_id, const std::string& prompt_content,
                     uint64_t generation, std::stop_token stop_token);

 private:
  [[nodiscard]] ChatRequest BuildRoundRequest(
      const ChatServiceRequestBuilder& request_builder) const;
  void RunToolRound(
      const std::vector<ToolCallRequest>& requested_tools,
      const std::unordered_map<std::string, ChatMessageId>& streaming_card_ids,
      std::stop_token stop_token);
  [[nodiscard]] static ::yac::tool_call::ToolExecutionResult
  MakeRejectedToolResult(const ::yac::tool_call::PreparedToolCall& prepared);

  provider::ProviderRegistry* registry_;
  ::yac::tool_call::ToolExecutor* tool_executor_;
  ChatServiceToolApproval* tool_approval_;
  ChatServiceMcp* chat_service_mcp_;
  std::mutex* history_mutex_;
  std::vector<ChatMessage>* history_;
  EmitEventFn emit_event_;
  NextMessageIdFn next_message_id_;
  ConfigSnapshotFn config_snapshot_;
  GenerationValueFn generation_value_;
  std::set<std::string> excluded_tools_;
  std::mutex* approval_gate_;
  ModeExcludedToolsFn mode_excluded_tools_;
  PrepareBuiltInToolCallFn prepare_built_in_tool_call_;
  ExecuteBuiltInToolCallFn execute_built_in_tool_call_;
};

}  // namespace yac::chat::internal
