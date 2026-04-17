#include "chat/chat_service_history.hpp"

namespace yac::chat::internal {

ChatServiceHistory::ChatServiceHistory(std::vector<ChatMessage>& history)
    : history_(&history) {}

void ChatServiceHistory::AppendActiveUserMessage(ChatMessageId id,
                                                 const std::string& content) {
  history_->push_back(ChatMessage{.id = id,
                                  .role = ChatRole::User,
                                  .status = ChatMessageStatus::Active,
                                  .content = content});
}

void ChatServiceHistory::AppendAssistantToolRound(
    ChatMessageId assistant_id, const std::string& content,
    const std::vector<ToolCallRequest>& tool_calls) {
  history_->push_back(ChatMessage{.id = assistant_id,
                                  .role = ChatRole::Assistant,
                                  .status = ChatMessageStatus::Complete,
                                  .content = content,
                                  .tool_calls = tool_calls});
}

void ChatServiceHistory::AppendToolResult(
    ChatMessageId tool_message_id, const ToolCallRequest& tool_request,
    const ::yac::tool_call::ToolExecutionResult& result) {
  history_->push_back(ChatMessage{
      .id = tool_message_id,
      .role = ChatRole::Tool,
      .status = result.is_error ? ChatMessageStatus::Error
                                : ChatMessageStatus::Complete,
      .content = result.result_json,
      .tool_call_id = tool_request.id,
      .tool_name = tool_request.name,
  });
}

void ChatServiceHistory::AppendFinalAssistantMessage(
    ChatMessageId assistant_id, const std::string& content) {
  history_->push_back(ChatMessage{.id = assistant_id,
                                  .role = ChatRole::Assistant,
                                  .status = ChatMessageStatus::Complete,
                                  .content = content});
}

}  // namespace yac::chat::internal
