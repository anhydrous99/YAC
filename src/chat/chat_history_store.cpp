#include "chat/chat_history_store.hpp"

#include "chat/chat_service_compactor.hpp"
#include "chat/chat_service_history.hpp"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace yac::chat {

ChatHistoryStore::ChatHistoryStore(std::mutex& history_mutex)
    : history_mutex_(&history_mutex) {}

void ChatHistoryStore::AppendActiveUserMessage(ChatMessageId id,
                                               const std::string& content) {
  internal::ChatServiceHistory(history_).AppendActiveUserMessage(id, content);
}

void ChatHistoryStore::AppendAssistantToolRound(
    ChatMessageId assistant_id, const std::string& content,
    const std::vector<ToolCallRequest>& tool_calls) {
  internal::ChatServiceHistory(history_).AppendAssistantToolRound(
      assistant_id, content, tool_calls);
}

void ChatHistoryStore::AppendToolResult(
    ChatMessageId tool_message_id, const ToolCallRequest& tool_request,
    const ::yac::tool_call::ToolExecutionResult& result) {
  internal::ChatServiceHistory(history_).AppendToolResult(tool_message_id,
                                                          tool_request, result);
}

void ChatHistoryStore::AppendFinalAssistantMessage(ChatMessageId assistant_id,
                                                   const std::string& content) {
  internal::ChatServiceHistory(history_).AppendFinalAssistantMessage(
      assistant_id, content);
}

void ChatHistoryStore::Append(ChatMessage message) {
  history_.push_back(std::move(message));
}

const std::vector<ChatMessage>& ChatHistoryStore::View() const {
  return history_;
}

std::vector<ChatMessage>& ChatHistoryStore::MutableView() {
  return history_;
}

void ChatHistoryStore::Clear() {
  history_.clear();
}

std::size_t ChatHistoryStore::Compact(std::size_t keep_last) {
  const auto before = CountNonSystem();
  internal::CompactHistory(history_, keep_last);
  return before > keep_last ? before - keep_last : 0;
}

bool ChatHistoryStore::HasNonSystemMessages() const {
  return std::ranges::any_of(history_, [](const ChatMessage& message) {
    return message.role != ChatRole::System;
  });
}

std::size_t ChatHistoryStore::CountNonSystem() const {
  return static_cast<std::size_t>(
      std::ranges::count_if(history_, [](const ChatMessage& message) {
        return message.role != ChatRole::System;
      }));
}

std::vector<ChatMessage> ChatHistoryStore::Snapshot() const {
  std::scoped_lock lock(*history_mutex_);
  return history_;
}

internal::CompactionOutcome ChatHistoryStore::MaybeAutoCompact(
    const ChatConfig& config, provider::LanguageModelProvider& provider,
    const std::function<void(ChatEvent)>& emit_event,
    std::stop_token stop_token) {
  return internal::MaybeAutoCompactHistory(history_, *history_mutex_, config,
                                           provider, emit_event,
                                           std::move(stop_token));
}

void ChatHistoryStore::FilterToolsForAgentMode(
    std::vector<ToolDefinition>& tools,
    const std::set<std::string>& static_excluded,
    const std::set<std::string>& mode_excluded) {
  std::erase_if(tools, [&](const ToolDefinition& definition) {
    return static_excluded.contains(definition.name) ||
           mode_excluded.contains(definition.name);
  });
}

}  // namespace yac::chat
