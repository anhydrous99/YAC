#pragma once

#include "core_types/chat_ids.hpp"
#include "core_types/tool_call_types.hpp"

#include <chrono>
#include <string>
#include <utility>
#include <variant>

namespace yac::presentation {

enum class Sender { User, Agent, Tool };

using MessageId = chat::ChatMessageId;
using MessageStatus = chat::ChatMessageStatus;

struct TextContent {
  std::string text;
};

struct ToolContent {
  ::yac::tool_call::ToolCallBlock block;
};

using MessageContent = std::variant<TextContent, ToolContent>;

struct Message {
  MessageId id = 0;
  Sender sender = Sender::User;
  MessageContent body = TextContent{};
  MessageStatus status = MessageStatus::Complete;
  std::string role_label;
  std::string timestamp;
  std::chrono::system_clock::time_point created_at =
      std::chrono::system_clock::now();

  Message() = default;
  Message(Sender sender, std::string content, std::string role_label = "",
          std::string timestamp = "");

  [[nodiscard]] static Message Tool(::yac::tool_call::ToolCallBlock block);

  [[nodiscard]] const std::string& Text() const;
  [[nodiscard]] std::string& Text();
  [[nodiscard]] const ::yac::tool_call::ToolCallBlock* ToolCall() const;
  [[nodiscard]] ::yac::tool_call::ToolCallBlock* ToolCall();
  [[nodiscard]] std::string DisplayLabel() const;
};

template <typename UserFn, typename AgentFn, typename ToolFn,
          typename DefaultFn>
decltype(auto) SenderSwitch(Sender sender, UserFn&& when_user,
                            AgentFn&& when_agent, ToolFn&& when_tool,
                            DefaultFn&& default_value) {
  switch (sender) {
    case Sender::User:
      return std::forward<UserFn>(when_user)();
    case Sender::Agent:
      return std::forward<AgentFn>(when_agent)();
    case Sender::Tool:
      return std::forward<ToolFn>(when_tool)();
    default:
      return std::forward<DefaultFn>(default_value)();
  }
}

template <typename UserFn, typename AgentFn, typename ToolFn>
decltype(auto) SenderSwitch(Sender sender, UserFn&& when_user,
                            AgentFn&& when_agent, ToolFn&& when_tool) {
  switch (sender) {
    case Sender::User:
      return std::forward<UserFn>(when_user)();
    case Sender::Agent:
      return std::forward<AgentFn>(when_agent)();
    case Sender::Tool:
    default:
      return std::forward<ToolFn>(when_tool)();
  }
}

}  // namespace yac::presentation
