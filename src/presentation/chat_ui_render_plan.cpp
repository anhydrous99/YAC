#include "chat_ui_render_plan.hpp"

#include <algorithm>

namespace yac::presentation {

namespace {

MessageRenderItem StandaloneMessageItem(size_t message_index) {
  return MessageRenderItem{
      .kind = MessageRenderItem::Kind::StandaloneMessage,
      .message_index = message_index,
  };
}

MessageRenderItem StandaloneToolItem(size_t message_index,
                                     size_t tool_state_index) {
  return MessageRenderItem{
      .kind = MessageRenderItem::Kind::StandaloneTool,
      .message_index = message_index,
      .tool_state_index = tool_state_index,
  };
}

MessageRenderItem AgentGroupItem(size_t agent_index,
                                 std::vector<ToolRenderRef> tools,
                                 size_t group_ordinal,
                                 const std::vector<Message>& messages) {
  const bool any_tool_active =
      std::any_of(tools.begin(), tools.end(), [&messages](const auto& tool) {
        return messages[tool.message_index].status == MessageStatus::Active;
      });
  return MessageRenderItem{
      .kind = MessageRenderItem::Kind::AgentGroup,
      .message_index = agent_index,
      .group_ordinal = group_ordinal,
      .any_tool_active = any_tool_active,
      .tools = std::move(tools),
  };
}

}  // namespace

std::vector<MessageRenderItem> BuildMessageRenderPlan(
    const std::vector<Message>& messages) {
  std::vector<MessageRenderItem> plan;
  plan.reserve(messages.size());

  size_t next_tool_state_index = 0;
  size_t next_group_ordinal = 0;
  size_t index = 0;
  while (index < messages.size()) {
    const auto& message = messages[index];
    if (message.sender == Sender::Agent) {
      std::vector<ToolRenderRef> tools;
      size_t cursor = index + 1;
      while (cursor < messages.size() && messages[cursor].sender == Sender::Tool) {
        tools.push_back(ToolRenderRef{
            .message_index = cursor,
            .tool_state_index = next_tool_state_index++,
        });
        ++cursor;
      }
      if (tools.empty()) {
        plan.push_back(StandaloneMessageItem(index));
        ++index;
        continue;
      }

      plan.push_back(AgentGroupItem(index, std::move(tools), next_group_ordinal,
                                    messages));
      ++next_group_ordinal;
      index = cursor;
      continue;
    }

    if (message.sender == Sender::Tool) {
      plan.push_back(StandaloneToolItem(index, next_tool_state_index++));
    } else {
      plan.push_back(StandaloneMessageItem(index));
    }
    ++index;
  }

  return plan;
}

}  // namespace yac::presentation
