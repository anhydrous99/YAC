#pragma once

#include "message.hpp"

#include <string>

namespace yac::presentation {

class ChatEventSink {
 public:
  ChatEventSink() = default;
  virtual ~ChatEventSink() = default;
  ChatEventSink(const ChatEventSink&) = default;
  ChatEventSink(ChatEventSink&&) = default;
  ChatEventSink& operator=(const ChatEventSink&) = default;
  ChatEventSink& operator=(ChatEventSink&&) = default;

  virtual MessageId AddMessageWithId(MessageId id, Sender sender,
                                     std::string content,
                                     MessageStatus status) = 0;
  virtual MessageId StartAgentMessage(MessageId id) = 0;
  virtual void AppendToAgentMessage(MessageId id, std::string delta) = 0;
  virtual void SetMessageStatus(MessageId id, MessageStatus status) = 0;
  virtual void AddToolCallMessageWithId(MessageId id,
                                        ::yac::tool_call::ToolCallBlock block,
                                        MessageStatus status) = 0;
  virtual void UpdateToolCallMessage(MessageId id,
                                     ::yac::tool_call::ToolCallBlock block,
                                     MessageStatus status) = 0;
  virtual void ShowToolApproval(std::string approval_id, std::string tool_name,
                                std::string prompt) = 0;
  virtual void SetProviderModel(std::string provider_id, std::string model) = 0;
  virtual void SetTyping(bool typing) = 0;
  virtual void ClearMessages() = 0;

  [[nodiscard]] virtual bool HasMessage(MessageId id) const = 0;
  [[nodiscard]] virtual std::string Model() const = 0;
};

}  // namespace yac::presentation
