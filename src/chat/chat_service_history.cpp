#include "chat/chat_service_history.hpp"

#include <algorithm>
#include <string>

namespace yac::chat::internal {

void CompactHistory(std::vector<ChatMessage>& history,
                    decltype(sizeof(0)) keep_last) {
  const auto non_system = static_cast<decltype(sizeof(0))>(std::count_if(
      history.begin(), history.end(), [](const ChatMessage& message) {
        return message.role != ChatRole::System;
      }));
  if (non_system <= keep_last) {
    return;
  }

  const decltype(sizeof(0)) to_remove = non_system - keep_last;
  const auto first_non_system = std::find_if(
      history.begin(), history.end(), [](const ChatMessage& message) {
        return message.role != ChatRole::System;
      });

  std::vector<ChatMessage> compacted;
  compacted.reserve(history.size() - to_remove + 1);
  compacted.insert(compacted.end(), history.begin(), first_non_system);

  compacted.push_back(ChatMessage{
      .role = ChatRole::System,
      .status = ChatMessageStatus::Complete,
      .content = "[Earlier conversation compacted. " +
                 std::to_string(to_remove) + " messages removed.]"});

  decltype(sizeof(0)) removed = 0;
  for (auto it = first_non_system; it != history.end(); ++it) {
    if (it->role != ChatRole::System && removed < to_remove) {
      ++removed;
      continue;
    }
    compacted.push_back(*it);
  }

  history = std::move(compacted);
}

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
