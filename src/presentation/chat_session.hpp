#pragma once

#include "message.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace yac::presentation {

class ChatSession {
 public:
  MessageId AddMessage(Sender sender, std::string content,
                       MessageStatus status = MessageStatus::Complete);
  MessageId AddMessageWithId(MessageId id, Sender sender, std::string content,
                             MessageStatus status = MessageStatus::Complete);
  MessageId AddToolCallMessage(::yac::tool_call::ToolCallBlock block);
  void AppendToAgentMessage(MessageId id, std::string delta);
  void SetMessageStatus(MessageId id, MessageStatus status);
  void SetToolExpanded(size_t index, bool expanded);
  void ClearMessages();

  [[nodiscard]] std::optional<size_t> FindMessageIndex(MessageId id) const;
  [[nodiscard]] const std::vector<Message>& Messages() const;
  [[nodiscard]] bool HasMessage(MessageId id) const;
  [[nodiscard]] bool Empty() const;
  [[nodiscard]] size_t MessageCount() const;
  [[nodiscard]] bool* ToolExpandedState(size_t index);

 private:
  MessageId next_id_ = 1;
  std::vector<Message> messages_;
  std::vector<std::unique_ptr<bool>> tool_expanded_states_;
};

}  // namespace yac::presentation
