#include "chat_session.hpp"

#include <utility>

namespace yac::presentation {

MessageId ChatSession::AddMessage(Sender sender, std::string content,
                                  MessageStatus status) {
  auto id = next_id_++;
  return AddMessageWithId(id, sender, std::move(content), status);
}

MessageId ChatSession::AddMessageWithId(MessageId id, Sender sender,
                                        std::string content,
                                        MessageStatus status) {
  if (id >= next_id_) {
    next_id_ = id + 1;
  }
  Message message{sender, std::move(content)};
  message.id = id;
  message.status = status;
  messages_.push_back(std::move(message));
  return id;
}

MessageId ChatSession::AddToolCallMessage(
    ::yac::tool_call::ToolCallBlock block) {
  auto id = next_id_++;
  return AddToolCallMessageWithId(id, std::move(block),
                                  MessageStatus::Complete);
}

MessageId ChatSession::AddToolCallMessageWithId(
    MessageId id, ::yac::tool_call::ToolCallBlock block, MessageStatus status) {
  if (id >= next_id_) {
    next_id_ = id + 1;
  }
  auto message = Message::Tool(std::move(block));
  message.id = id;
  message.status = status;
  messages_.push_back(std::move(message));
  tool_expanded_states_.push_back(std::make_unique<bool>(true));
  return id;
}

void ChatSession::AppendToAgentMessage(MessageId id, std::string delta) {
  if (delta.empty()) {
    return;
  }

  auto idx = FindMessageIndex(id);
  if (!idx.has_value()) {
    return;
  }

  auto& message = messages_[*idx];
  message.Text() += delta;
}

void ChatSession::SetMessageStatus(MessageId id, MessageStatus status) {
  auto idx = FindMessageIndex(id);
  if (!idx.has_value()) {
    return;
  }

  auto& message = messages_[*idx];
  if (message.status == status) {
    return;
  }
  message.status = status;
}

void ChatSession::SetToolCallMessage(MessageId id,
                                     ::yac::tool_call::ToolCallBlock block,
                                     MessageStatus status) {
  auto idx = FindMessageIndex(id);
  if (!idx.has_value()) {
    return;
  }

  auto& message = messages_[*idx];
  message.body = ToolContent{std::move(block)};
  message.status = status;
}

void ChatSession::SetToolExpanded(size_t index, bool expanded) {
  if (index >= tool_expanded_states_.size()) {
    return;
  }

  *tool_expanded_states_[index] = expanded;
}

void ChatSession::ClearMessages() {
  messages_.clear();
  tool_expanded_states_.clear();
}

std::optional<size_t> ChatSession::FindMessageIndex(MessageId id) const {
  for (size_t i = 0; i < messages_.size(); ++i) {
    if (messages_[i].id == id) {
      return i;
    }
  }
  return std::nullopt;
}

const std::vector<Message>& ChatSession::Messages() const {
  return messages_;
}

bool ChatSession::HasMessage(MessageId id) const {
  return FindMessageIndex(id).has_value();
}

bool ChatSession::Empty() const {
  return messages_.empty();
}

size_t ChatSession::MessageCount() const {
  return messages_.size();
}

bool* ChatSession::ToolExpandedState(size_t index) {
  while (tool_expanded_states_.size() <= index) {
    tool_expanded_states_.push_back(std::make_unique<bool>(false));
  }
  return tool_expanded_states_[index].get();
}

}  // namespace yac::presentation
