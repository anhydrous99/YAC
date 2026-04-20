#pragma once

#include "message.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace yac::presentation {

struct SubAgentToolMessage {
  SubAgentToolMessage(std::string tool_call_id, std::string tool_name,
                      ::yac::tool_call::ToolCallBlock block,
                      MessageStatus status);
  SubAgentToolMessage(const SubAgentToolMessage&) = delete;
  SubAgentToolMessage& operator=(const SubAgentToolMessage&) = delete;
  SubAgentToolMessage(SubAgentToolMessage&&) noexcept = default;
  SubAgentToolMessage& operator=(SubAgentToolMessage&&) noexcept = default;

  std::string tool_call_id;
  std::string tool_name;
  ::yac::tool_call::ToolCallBlock block;
  MessageStatus status = MessageStatus::Complete;
  std::unique_ptr<bool> expanded;
};

class ChatSession {
 public:
  MessageId AddMessage(Sender sender, std::string content,
                       MessageStatus status = MessageStatus::Complete);
  MessageId AddMessageWithId(MessageId id, Sender sender, std::string content,
                             MessageStatus status = MessageStatus::Complete);
  MessageId AddToolCallMessage(::yac::tool_call::ToolCallBlock block);
  MessageId AddToolCallMessageWithId(MessageId id,
                                     ::yac::tool_call::ToolCallBlock block,
                                     MessageStatus status);
  void AppendToAgentMessage(MessageId id, std::string delta);
  void SetMessageStatus(MessageId id, MessageStatus status);
  void SetToolCallMessage(MessageId id, ::yac::tool_call::ToolCallBlock block,
                          MessageStatus status);
  [[nodiscard]] bool UpsertSubAgentToolCall(
      MessageId parent_id, std::string tool_call_id, std::string tool_name,
      ::yac::tool_call::ToolCallBlock block, MessageStatus status);
  void SetToolExpanded(size_t index, bool expanded);
  void ClearMessages();

  [[nodiscard]] std::optional<size_t> FindMessageIndex(MessageId id) const;
  [[nodiscard]] const std::vector<Message>& Messages() const;
  [[nodiscard]] bool HasMessage(MessageId id) const;
  [[nodiscard]] bool Empty() const;
  [[nodiscard]] size_t MessageCount() const;
  [[nodiscard]] bool* ToolExpandedState(size_t index);
  [[nodiscard]] bool* GroupExpandedState(size_t group_index,
                                         bool default_expanded);
  [[nodiscard]] const std::vector<SubAgentToolMessage>& SubAgentToolCalls(
      MessageId parent_id) const;
  [[nodiscard]] const SubAgentToolMessage* SubAgentToolCall(
      MessageId parent_id, size_t child_index) const;
  [[nodiscard]] bool* SubAgentToolExpandedState(MessageId parent_id,
                                                size_t child_index);

 private:
  MessageId next_id_ = 1;
  std::vector<Message> messages_;
  std::vector<std::unique_ptr<bool>> tool_expanded_states_;
  std::vector<std::unique_ptr<bool>> group_expanded_states_;
  std::unordered_map<MessageId, std::vector<SubAgentToolMessage>>
      sub_agent_tool_messages_;
};

}  // namespace yac::presentation
