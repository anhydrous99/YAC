#pragma once

#include "message.hpp"

#include <cstddef>
#include <cstdint>
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
                             MessageStatus status = MessageStatus::Complete,
                             std::string role_label = "");
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
  [[nodiscard]] uint64_t PlanGeneration() const;
  // Bumps on every mutation that changes rendered text / tool-call content,
  // including streaming text appends. Paired with PlanGeneration(), gives
  // callers a cache key that invalidates iff the rendered height could
  // change.
  [[nodiscard]] uint64_t ContentGeneration() const;
  [[nodiscard]] bool HasActiveAgent() const;
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
  // Bumped on mutations that change the render plan shape (add, status,
  // tool-call body, clear). Text appends do NOT bump — they never change
  // sender/status/ordering, which is all BuildMessageRenderPlan reads.
  uint64_t plan_generation_ = 0;
  // Bumped by every mutation that changes rendered content (including text
  // appends). Used by ChatUI to invalidate its content-height measurement
  // cache.
  uint64_t content_generation_ = 0;
  std::vector<Message> messages_;
  // Parallel to messages_: maps MessageId → index into messages_. Keeps
  // FindMessageIndex / HasMessage at O(1). Safe because messages_ is
  // append-only + clear (no reorder, no single-message delete). A future
  // method that removes or reorders messages must update this map.
  std::unordered_map<MessageId, size_t> id_to_index_;
  // Count of messages where sender == Agent && status == Active. Maintained
  // by the Add/SetMessageStatus/Clear paths. HasActiveAgent() reads this in
  // O(1). Any future code path that mutates a message's sender or status
  // outside SetMessageStatus must update this counter.
  int active_agent_count_ = 0;
  std::vector<std::unique_ptr<bool>> tool_expanded_states_;
  std::vector<std::unique_ptr<bool>> group_expanded_states_;
  std::unordered_map<MessageId, std::vector<SubAgentToolMessage>>
      sub_agent_tool_messages_;
};

}  // namespace yac::presentation
