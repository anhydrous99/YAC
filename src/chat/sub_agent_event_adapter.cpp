#include "chat/sub_agent_event_adapter.hpp"

#include <utility>

namespace yac::chat {

namespace {

std::string TruncateWithEllipsis(const std::string& text, size_t max_chars) {
  if (text.size() <= max_chars) {
    return text;
  }
  if (max_chars <= 3) {
    return text.substr(0, max_chars);
  }
  return text.substr(0, max_chars - 3) + "...";
}

}  // namespace

std::optional<ChatEvent> AdaptSubAgentPromptEvent(
    const SubAgentEventContext& context, ChatEvent event,
    std::atomic<int>& completed_tool_count) {
  if (auto* approval = event.As<ToolApprovalRequestedEvent>()) {
    approval->message_id = context.card_message_id;
    approval->text = "[Sub-agent: " + TruncateWithEllipsis(context.task, 48) +
                     "] " + approval->text;
    return event;
  }

  if (auto* started = event.As<ToolCallStartedEvent>()) {
    auto tool_call_id = started->tool_call_id.value.empty()
                            ? ToolCallId{context.agent_id + ":" +
                                         std::to_string(started->message_id)}
                            : std::move(started->tool_call_id);
    return ChatEvent{SubAgentProgressEvent{
        .message_id = context.card_message_id,
        .sub_agent_id = context.agent_id,
        .sub_agent_task = context.task,
        .sub_agent_tool_count = completed_tool_count.load(),
        .status = ChatMessageStatus::Active,
        .child_tool =
            SubAgentChildToolEvent{.tool_call_id = std::move(tool_call_id),
                                   .tool_name = std::move(started->tool_name),
                                   .tool_call = std::move(started->tool_call),
                                   .status = started->status}}};
  }

  if (auto* done = event.As<ToolCallDoneEvent>()) {
    auto tool_call_id = done->tool_call_id.value.empty()
                            ? ToolCallId{context.agent_id + ":" +
                                         std::to_string(done->message_id)}
                            : std::move(done->tool_call_id);
    return ChatEvent{SubAgentProgressEvent{
        .message_id = context.card_message_id,
        .sub_agent_id = context.agent_id,
        .sub_agent_task = context.task,
        .sub_agent_tool_count = completed_tool_count.fetch_add(1) + 1,
        .status = ChatMessageStatus::Active,
        .child_tool =
            SubAgentChildToolEvent{.tool_call_id = std::move(tool_call_id),
                                   .tool_name = std::move(done->tool_name),
                                   .tool_call = std::move(done->tool_call),
                                   .status = done->status}}};
  }

  return std::nullopt;
}

ChatEvent MakeSubAgentCompletionEvent(const SubAgentCompletionEventData& data) {
  switch (data.type) {
    case ChatEventType::SubAgentCancelled:
      return ChatEvent{
          SubAgentCancelledEvent{.message_id = data.message_id,
                                 .sub_agent_id = data.sub_agent_id,
                                 .sub_agent_task = data.sub_agent_task}};
    case ChatEventType::SubAgentError:
      return ChatEvent{SubAgentErrorEvent{
          .message_id = data.message_id,
          .sub_agent_id = data.sub_agent_id,
          .sub_agent_task = data.sub_agent_task,
          .sub_agent_result = data.sub_agent_result,
          .sub_agent_tool_count = data.sub_agent_tool_count,
          .sub_agent_elapsed_ms = data.sub_agent_elapsed_ms}};
    case ChatEventType::SubAgentCompleted:
    default:
      return ChatEvent{SubAgentCompletedEvent{
          .message_id = data.message_id,
          .sub_agent_id = data.sub_agent_id,
          .sub_agent_task = data.sub_agent_task,
          .sub_agent_result = data.sub_agent_result,
          .sub_agent_tool_count = data.sub_agent_tool_count,
          .sub_agent_elapsed_ms = data.sub_agent_elapsed_ms}};
  }
}

}  // namespace yac::chat
