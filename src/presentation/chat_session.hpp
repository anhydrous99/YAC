#pragma once

#include "core_types/typed_ids.hpp"
#include "message.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace yac::presentation {

struct SubAgentToolMessage {
  SubAgentToolMessage(::yac::ToolCallId tool_call_id, std::string tool_name,
                      ::yac::tool_call::ToolCallBlock block,
                      MessageStatus status);
  ~SubAgentToolMessage() = default;
  SubAgentToolMessage(const SubAgentToolMessage&) = delete;
  SubAgentToolMessage& operator=(const SubAgentToolMessage&) = delete;
  SubAgentToolMessage(SubAgentToolMessage&&) noexcept = default;
  SubAgentToolMessage& operator=(SubAgentToolMessage&&) noexcept = default;

  ::yac::ToolCallId tool_call_id;
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

  // Append a tool segment to the active agent message. If no agent is
  // active, a new Active agent message is opened first so the segment has
  // a parent.
  void AddToolCallSegment(MessageId tool_id,
                          ::yac::tool_call::ToolCallBlock block,
                          MessageStatus status);
  // Convenience overload that mints a fresh tool id; returns it.
  MessageId AddToolCallSegment(::yac::tool_call::ToolCallBlock block,
                               MessageStatus status = MessageStatus::Complete);
  // Update an existing tool segment found by its id.
  void UpdateToolCallSegment(MessageId tool_id,
                             ::yac::tool_call::ToolCallBlock block,
                             MessageStatus status);

  void AppendToAgentMessage(MessageId id, std::string delta);
  void SetMessageStatus(MessageId id, MessageStatus status);
  [[nodiscard]] bool UpsertSubAgentToolCall(
      MessageId parent_id, ::yac::ToolCallId tool_call_id,
      std::string tool_name, ::yac::tool_call::ToolCallBlock block,
      MessageStatus status);
  void SetToolExpanded(MessageId tool_id, bool expanded);
  void ClearMessages();

  [[nodiscard]] std::optional<size_t> FindMessageIndex(MessageId id) const;
  [[nodiscard]] const std::vector<Message>& Messages() const;
  [[nodiscard]] bool HasMessage(MessageId id) const;
  [[nodiscard]] bool HasToolSegment(MessageId tool_id) const;
  [[nodiscard]] const ToolSegment* FindToolSegment(MessageId tool_id) const;
  [[nodiscard]] bool Empty() const;
  [[nodiscard]] size_t MessageCount() const;
  [[nodiscard]] uint64_t PlanGeneration() const;
  // Bumps on every mutation that changes rendered text / tool-call content,
  // including streaming text appends. Paired with PlanGeneration(), gives
  // callers a cache key that invalidates iff the rendered height could
  // change.
  [[nodiscard]] uint64_t ContentGeneration() const;
  [[nodiscard]] bool HasActiveAgent() const;
  [[nodiscard]] std::optional<MessageId> ActiveAgentId() const;
  // Returns the index (among text-only segments) of the trailing text
  // segment of the given message, or nullopt if the message doesn't exist
  // or has no text segments.
  [[nodiscard]] std::optional<size_t> TrailingTextSegmentIndex(
      MessageId id) const;
  [[nodiscard]] bool* ToolExpandedState(MessageId tool_id);
  [[nodiscard]] const std::vector<SubAgentToolMessage>& SubAgentToolCalls(
      MessageId parent_id) const;
  [[nodiscard]] const SubAgentToolMessage* SubAgentToolCall(
      MessageId parent_id, size_t child_index) const;
  [[nodiscard]] bool* SubAgentToolExpandedState(MessageId parent_id,
                                                size_t child_index);

 private:
  // Returns the location of an existing agent message we should append a
  // segment to: prefers the active agent; otherwise opens a fresh Active
  // agent message and returns its index.
  size_t EnsureAgentTurn();

  MessageId next_id_ = 1;
  // Bumped on mutations that change the render plan shape (add, status,
  // tool segment add/update, clear). Text appends do NOT bump — they never
  // change segment vector size, status, or sender.
  uint64_t plan_generation_ = 0;
  // Bumped by every mutation that changes rendered content.
  uint64_t content_generation_ = 0;
  std::vector<Message> messages_;
  std::unordered_map<MessageId, size_t> id_to_index_;
  // tool_id → (message_index, segment_index)
  std::unordered_map<MessageId, std::pair<size_t, size_t>> tool_location_;
  // Tracks the message id of the most recently opened Active agent message,
  // cleared when its status leaves Active. Used to attach incoming tool
  // segments to the right turn.
  std::optional<MessageId> active_agent_id_;
  int active_agent_count_ = 0;
  std::unordered_map<MessageId, std::unique_ptr<bool>> tool_expanded_;
  std::unordered_map<MessageId, std::vector<SubAgentToolMessage>>
      sub_agent_tool_messages_;
};

}  // namespace yac::presentation
