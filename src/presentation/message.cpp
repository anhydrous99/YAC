#include "message.hpp"

#include "tool_call/renderer.hpp"

#include <utility>

namespace yac::presentation {

Message::Message(Sender sender, std::string content, std::string role_label,
                 std::string timestamp)
    : sender(sender),
      body(TextContent{std::move(content)}),
      role_label(std::move(role_label)),
      timestamp(std::move(timestamp)) {}

Message Message::Tool(::yac::tool_call::ToolCallBlock block) {
  Message message;
  message.sender = Sender::Tool;
  message.body = ToolContent{std::move(block)};
  return message;
}

const std::string& Message::Text() const {
  static const std::string k_empty;
  const auto* text = std::get_if<TextContent>(&body);
  return text == nullptr ? k_empty : text->text;
}

std::string& Message::Text() {
  auto* text = std::get_if<TextContent>(&body);
  if (text == nullptr) {
    body = TextContent{};
    text = std::get_if<TextContent>(&body);
  }
  return text->text;
}

const ::yac::tool_call::ToolCallBlock* Message::ToolCall() const {
  const auto* tool = std::get_if<ToolContent>(&body);
  return tool == nullptr ? nullptr : &tool->block;
}

::yac::tool_call::ToolCallBlock* Message::ToolCall() {
  auto* tool = std::get_if<ToolContent>(&body);
  return tool == nullptr ? nullptr : &tool->block;
}

std::string Message::DisplayLabel() const {
  if (!role_label.empty()) {
    return role_label;
  }
  switch (sender) {
    case Sender::User:
      return "You";
    case Sender::Agent:
      return "Assistant";
    case Sender::Tool: {
      const auto* block = ToolCall();
      if (block != nullptr) {
        return tool_call::ToolCallRenderer::BuildLabel(*block);
      }
      return "Tool";
    }
  }

  return "Unknown";
}

}  // namespace yac::presentation
