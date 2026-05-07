#pragma once

#include "chat/types.hpp"
#include "core_types/typed_ids.hpp"

#include <atomic>
#include <optional>
#include <string>

namespace yac::chat {

struct SubAgentEventContext {
  ChatMessageId card_message_id = 0;
  std::string agent_id;
  std::string task;
};

struct SubAgentCompletionEventData {
  ChatEventType type = ChatEventType::SubAgentCompleted;
  ChatMessageId message_id = 0;
  ::yac::SubAgentId sub_agent_id;
  std::string sub_agent_task;
  std::string sub_agent_result;
  int sub_agent_tool_count = 0;
  int sub_agent_elapsed_ms = 0;
};

[[nodiscard]] std::optional<ChatEvent> AdaptSubAgentPromptEvent(
    const SubAgentEventContext& context, ChatEvent event,
    std::atomic<int>& completed_tool_count);

[[nodiscard]] ChatEvent MakeSubAgentCompletionEvent(
    const SubAgentCompletionEventData& data);

}  // namespace yac::chat
