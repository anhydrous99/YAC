#pragma once

#include "core_types/typed_ids.hpp"
#include "message.hpp"
#include "ui_status.hpp"

#include <optional>
#include <string>

namespace yac::presentation {

struct UsageStats;

class ChatEventSink {
 public:
  ChatEventSink() = default;
  virtual ~ChatEventSink() = default;
  ChatEventSink(const ChatEventSink&) = default;
  ChatEventSink(ChatEventSink&&) = default;
  ChatEventSink& operator=(const ChatEventSink&) = default;
  ChatEventSink& operator=(ChatEventSink&&) = default;

  virtual MessageId AddMessageWithId(MessageId id, Sender sender,
                                     std::string content, MessageStatus status,
                                     std::string role_label = "") = 0;
  virtual MessageId StartAgentMessage(MessageId id) = 0;
  virtual void AppendToAgentMessage(MessageId id, std::string delta) = 0;
  virtual void SetMessageStatus(MessageId id, MessageStatus status) = 0;
  virtual void AddToolCallSegment(MessageId tool_id,
                                  ::yac::tool_call::ToolCallBlock block,
                                  MessageStatus status) = 0;
  virtual void UpdateToolCallMessage(MessageId tool_id,
                                     ::yac::tool_call::ToolCallBlock block,
                                     MessageStatus status) = 0;
  virtual void UpdateSubAgentToolCallMessage(
      MessageId parent_id, ::yac::ToolCallId tool_call_id,
      std::string tool_name, ::yac::tool_call::ToolCallBlock block,
      MessageStatus status) = 0;
  virtual void ShowToolApproval(
      ::yac::ApprovalId approval_id, std::string tool_name, std::string prompt,
      std::optional<::yac::tool_call::ToolCallBlock> preview) = 0;
  virtual void ShowAskUserDialog(::yac::ApprovalId approval_id,
                                 std::string question,
                                 std::vector<std::string> options) = 0;
  virtual void SetProviderModel(::yac::ProviderId provider_id,
                                ::yac::ModelId model) = 0;
  virtual void SetLastUsage(UsageStats usage) = 0;
  virtual void SetContextWindowTokens(int tokens) = 0;
  virtual void SetQueueDepth(int queue_depth) = 0;
  virtual void SetTransientStatus(UiNotice notice) = 0;
  virtual void SetTyping(bool typing) = 0;
  virtual void ClearMessages() = 0;

  [[nodiscard]] virtual bool HasMessage(MessageId id) const = 0;
  [[nodiscard]] virtual bool HasToolSegment(MessageId tool_id) const = 0;
  [[nodiscard]] virtual std::string Model() const = 0;
};

}  // namespace yac::presentation
