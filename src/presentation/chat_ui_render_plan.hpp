#pragma once

#include "message.hpp"

#include <cstddef>
#include <vector>

namespace yac::presentation {

struct ToolRenderRef {
  size_t message_index = 0;
  size_t tool_state_index = 0;

  auto operator==(const ToolRenderRef&) const -> bool = default;
};

struct MessageRenderItem {
  enum class Kind {
    StandaloneMessage,
    StandaloneTool,
    AgentGroup,
  };

  Kind kind = Kind::StandaloneMessage;
  size_t message_index = 0;
  size_t tool_state_index = 0;
  size_t group_ordinal = 0;
  bool any_tool_active = false;
  std::vector<ToolRenderRef> tools;

  auto operator==(const MessageRenderItem&) const -> bool = default;
};

[[nodiscard]] std::vector<MessageRenderItem> BuildMessageRenderPlan(
    const std::vector<Message>& messages);

}  // namespace yac::presentation
