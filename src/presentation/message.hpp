#pragma once

#include "core_types/chat_ids.hpp"
#include "core_types/tool_call_types.hpp"

#include <chrono>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace yac::presentation {

enum class Sender { User, Agent };

using MessageId = chat::ChatMessageId;
using MessageStatus = chat::ChatMessageStatus;

struct TextSegment {
  std::string text;
};

struct ToolSegment {
  MessageId id = 0;
  ::yac::tool_call::ToolCallBlock block;
  MessageStatus status = MessageStatus::Complete;
};

using MessageSegment = std::variant<TextSegment, ToolSegment>;

struct Message {
  MessageId id = 0;
  Sender sender = Sender::User;
  std::vector<MessageSegment> segments;
  MessageStatus status = MessageStatus::Complete;
  std::string role_label;
  std::string timestamp;
  std::chrono::system_clock::time_point created_at =
      std::chrono::system_clock::now();

  Message() = default;
  Message(Sender sender, std::string content, std::string role_label = "",
          std::string timestamp = "");

  // Append `delta` to the trailing text segment, opening a new one if the
  // last segment is a tool (or the segments vector is empty). This is the
  // single point that makes interleaved text/tool emission preserve order.
  void AppendText(std::string delta);

  // Concatenation of every TextSegment. User messages always have exactly
  // one TextSegment, so this returns its text directly.
  [[nodiscard]] std::string CombinedText() const;
  [[nodiscard]] ToolSegment* FindToolSegment(MessageId tool_id);
  [[nodiscard]] const ToolSegment* FindToolSegment(MessageId tool_id) const;
  [[nodiscard]] std::string DisplayLabel() const;
};

template <typename UserFn, typename AgentFn>
decltype(auto) SenderSwitch(Sender sender, UserFn&& when_user,
                            AgentFn&& when_agent) {
  switch (sender) {
    case Sender::User:
      return std::forward<UserFn>(when_user)();
    case Sender::Agent:
    default:
      return std::forward<AgentFn>(when_agent)();
  }
}

}  // namespace yac::presentation
