#pragma once

#include "chat/types.hpp"
#include "tool_call/executor.hpp"

#include <string>
#include <vector>

namespace yac::chat::internal {

class ChatServiceHistory {
 public:
  explicit ChatServiceHistory(std::vector<ChatMessage>& history);

  void AppendActiveUserMessage(ChatMessageId id, const std::string& content);
  void AppendAssistantToolRound(ChatMessageId assistant_id,
                                const std::string& content,
                                const std::vector<ToolCallRequest>& tool_calls);
  void AppendToolResult(ChatMessageId tool_message_id,
                        const ToolCallRequest& tool_request,
                        const ::yac::tool_call::ToolExecutionResult& result);
  void AppendFinalAssistantMessage(ChatMessageId assistant_id,
                                   const std::string& content);

 private:
  std::vector<ChatMessage>* history_;
};

}  // namespace yac::chat::internal
