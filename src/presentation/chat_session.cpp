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
                                        MessageStatus status,
                                        std::string role_label) {
  if (id >= next_id_) {
    next_id_ = id + 1;
  }
  Message message{sender, std::move(content), std::move(role_label)};
  message.id = id;
  message.status = status;
  messages_.push_back(std::move(message));
  id_to_index_[id] = messages_.size() - 1;
  if (sender == Sender::Agent) {
    if (status == MessageStatus::Active) {
      ++active_agent_count_;
      active_agent_id_ = id;
    } else if (active_agent_id_.has_value() && *active_agent_id_ == id) {
      active_agent_id_.reset();
    }
  }
  ++plan_generation_;
  ++content_generation_;
  return id;
}

size_t ChatSession::EnsureAgentTurn() {
  if (active_agent_id_.has_value()) {
    if (auto idx = FindMessageIndex(*active_agent_id_); idx.has_value()) {
      return *idx;
    }
    // Active id pointed at a deleted/cleared message; fall through and
    // open a new turn.
    active_agent_id_.reset();
  }
  auto fresh_id = next_id_++;
  Message message{Sender::Agent, ""};
  message.id = fresh_id;
  message.status = MessageStatus::Active;
  // Drop the auto-pushed empty TextSegment so the first segment we attach
  // (likely a ToolSegment) is the visible leader.
  message.segments.clear();
  messages_.push_back(std::move(message));
  id_to_index_[fresh_id] = messages_.size() - 1;
  active_agent_id_ = fresh_id;
  ++active_agent_count_;
  ++plan_generation_;
  ++content_generation_;
  return messages_.size() - 1;
}

MessageId ChatSession::AddToolCallSegment(::yac::tool_call::ToolCallBlock block,
                                          MessageStatus status) {
  auto id = next_id_++;
  AddToolCallSegment(id, std::move(block), status);
  return id;
}

void ChatSession::AddToolCallSegment(MessageId tool_id,
                                     ::yac::tool_call::ToolCallBlock block,
                                     MessageStatus status) {
  if (tool_id >= next_id_) {
    next_id_ = tool_id + 1;
  }
  size_t agent_index = EnsureAgentTurn();
  auto& agent_message = messages_[agent_index];
  agent_message.segments.emplace_back(
      ToolSegment{tool_id, std::move(block), status});
  size_t segment_index = agent_message.segments.size() - 1;
  tool_location_[tool_id] = {agent_index, segment_index};
  tool_expanded_.emplace(tool_id, std::make_unique<bool>(false));
  ++plan_generation_;
  ++content_generation_;
}

void ChatSession::UpdateToolCallSegment(MessageId tool_id,
                                        ::yac::tool_call::ToolCallBlock block,
                                        MessageStatus status) {
  auto it = tool_location_.find(tool_id);
  if (it == tool_location_.end()) {
    return;
  }
  auto [msg_idx, seg_idx] = it->second;
  if (msg_idx >= messages_.size() ||
      seg_idx >= messages_[msg_idx].segments.size()) {
    return;
  }
  auto& segment = messages_[msg_idx].segments[seg_idx];
  auto* tool = std::get_if<ToolSegment>(&segment);
  if (tool == nullptr) {
    return;
  }
  tool->block = std::move(block);
  tool->status = status;
  ++plan_generation_;
  ++content_generation_;
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
  const bool opens_new_segment =
      message.segments.empty() ||
      !std::holds_alternative<TextSegment>(message.segments.back());
  message.AppendText(std::move(delta));
  if (opens_new_segment) {
    ++plan_generation_;
  }
  ++content_generation_;
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
  if (message.sender == Sender::Agent) {
    const bool was_active = message.status == MessageStatus::Active;
    const bool now_active = status == MessageStatus::Active;
    if (was_active && !now_active) {
      --active_agent_count_;
      if (active_agent_id_.has_value() && *active_agent_id_ == id) {
        active_agent_id_.reset();
      }
    } else if (!was_active && now_active) {
      ++active_agent_count_;
      active_agent_id_ = id;
    }
  }
  message.status = status;
  ++plan_generation_;
  ++content_generation_;
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
    ++content_generation_;
    return false;
  }

  child_tools.emplace_back(std::move(tool_call_id), std::move(tool_name),
                           std::move(block), status);
  ++content_generation_;
  return true;
}

void ChatSession::SetToolExpanded(MessageId tool_id, bool expanded) {
  auto it = tool_expanded_.find(tool_id);
  if (it == tool_expanded_.end()) {
    return;
  }
  *it->second = expanded;
}

void ChatSession::ClearMessages() {
  messages_.clear();
  id_to_index_.clear();
  tool_location_.clear();
  active_agent_id_.reset();
  active_agent_count_ = 0;
  tool_expanded_.clear();
  sub_agent_tool_messages_.clear();
  ++plan_generation_;
  ++content_generation_;
}

std::optional<size_t> ChatSession::FindMessageIndex(MessageId id) const {
  auto it = id_to_index_.find(id);
  if (it == id_to_index_.end()) {
    return std::nullopt;
  }
  return it->second;
}

const std::vector<Message>& ChatSession::Messages() const {
  return messages_;
}

bool ChatSession::HasMessage(MessageId id) const {
  return id_to_index_.find(id) != id_to_index_.end();
}

bool ChatSession::HasToolSegment(MessageId tool_id) const {
  return tool_location_.find(tool_id) != tool_location_.end();
}

const ToolSegment* ChatSession::FindToolSegment(MessageId tool_id) const {
  auto it = tool_location_.find(tool_id);
  if (it == tool_location_.end()) {
    return nullptr;
  }
  auto [msg_idx, seg_idx] = it->second;
  if (msg_idx >= messages_.size() ||
      seg_idx >= messages_[msg_idx].segments.size()) {
    return nullptr;
  }
  return std::get_if<ToolSegment>(&messages_[msg_idx].segments[seg_idx]);
}

bool ChatSession::Empty() const {
  return messages_.empty();
}

size_t ChatSession::MessageCount() const {
  return messages_.size();
}

uint64_t ChatSession::PlanGeneration() const {
  return plan_generation_;
}

uint64_t ChatSession::ContentGeneration() const {
  return content_generation_;
}

bool ChatSession::HasActiveAgent() const {
  return active_agent_count_ > 0;
}

std::optional<MessageId> ChatSession::ActiveAgentId() const {
  return active_agent_id_;
}

std::optional<size_t> ChatSession::TrailingTextSegmentIndex(
    MessageId id) const {
  auto loc = FindMessageIndex(id);
  if (!loc.has_value()) {
    return std::nullopt;
  }
  const auto& message = messages_[*loc];
  std::optional<size_t> result;
  size_t text_idx = 0;
  for (const auto& seg : message.segments) {
    if (std::holds_alternative<TextSegment>(seg)) {
      result = text_idx;
      ++text_idx;
    }
  }
  return result;
}

bool* ChatSession::ToolExpandedState(MessageId tool_id) {
  auto [it, _] =
      tool_expanded_.try_emplace(tool_id, std::make_unique<bool>(false));
  return it->second.get();
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
