#include "chat_session.hpp"

#include <algorithm>
#include <utility>

namespace yac::presentation {

SubAgentToolMessage::SubAgentToolMessage(std::string tool_call_id,
                                         std::string tool_name,
                                         ::yac::tool_call::ToolCallBlock block,
                                         MessageStatus status)
    : tool_call_id(std::move(tool_call_id)),
      tool_name(std::move(tool_name)),
      block(std::move(block)),
      status(status),
      expanded(std::make_unique<bool>(false)) {}

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
  tool_expanded_states_.push_back(std::make_unique<bool>(false));
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

bool ChatSession::UpsertSubAgentToolCall(MessageId parent_id,
                                         std::string tool_call_id,
                                         std::string tool_name,
                                         ::yac::tool_call::ToolCallBlock block,
                                         MessageStatus status) {
  auto& child_tools = sub_agent_tool_messages_[parent_id];
  if (tool_call_id.empty()) {
    tool_call_id = tool_name.empty()
                       ? "tool-" + std::to_string(child_tools.size() + 1)
                       : tool_name;
  }

  auto existing =
      std::find_if(child_tools.begin(), child_tools.end(),
                   [&tool_call_id](const SubAgentToolMessage& child) {
                     return child.tool_call_id == tool_call_id;
                   });
  if (existing != child_tools.end()) {
    existing->tool_name = std::move(tool_name);
    existing->block = std::move(block);
    existing->status = status;
    return false;
  }

  child_tools.emplace_back(std::move(tool_call_id), std::move(tool_name),
                           std::move(block), status);
  return true;
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
  group_expanded_states_.clear();
  sub_agent_tool_messages_.clear();
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

bool* ChatSession::GroupExpandedState(size_t group_index,
                                      bool default_expanded) {
  while (group_expanded_states_.size() <= group_index) {
    group_expanded_states_.push_back(std::make_unique<bool>(default_expanded));
  }
  return group_expanded_states_[group_index].get();
}

const std::vector<SubAgentToolMessage>& ChatSession::SubAgentToolCalls(
    MessageId parent_id) const {
  static const std::vector<SubAgentToolMessage> k_empty;
  const auto it = sub_agent_tool_messages_.find(parent_id);
  return it == sub_agent_tool_messages_.end() ? k_empty : it->second;
}

const SubAgentToolMessage* ChatSession::SubAgentToolCall(
    MessageId parent_id, size_t child_index) const {
  const auto it = sub_agent_tool_messages_.find(parent_id);
  if (it == sub_agent_tool_messages_.end() ||
      child_index >= it->second.size()) {
    return nullptr;
  }
  return &it->second[child_index];
}

bool* ChatSession::SubAgentToolExpandedState(MessageId parent_id,
                                             size_t child_index) {
  auto it = sub_agent_tool_messages_.find(parent_id);
  if (it == sub_agent_tool_messages_.end() ||
      child_index >= it->second.size()) {
    return nullptr;
  }
  return it->second[child_index].expanded.get();
}

}  // namespace yac::presentation
