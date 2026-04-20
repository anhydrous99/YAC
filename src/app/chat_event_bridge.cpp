#include "app/chat_event_bridge.hpp"

#include "app/model_context_windows.hpp"
#include "presentation/chat_ui_overlay_state.hpp"
#include "presentation/util/terminal.hpp"

#include <utility>

namespace yac::app {

namespace {

yac::presentation::Sender SenderForRole(yac::chat::ChatRole role) {
  using yac::chat::ChatRole;
  using yac::presentation::Sender;

  switch (role) {
    case ChatRole::User:
      return Sender::User;
    case ChatRole::Assistant:
    case ChatRole::System:
      return Sender::Agent;
    case ChatRole::Tool:
      return Sender::Tool;
  }
  return Sender::Agent;
}

}  // namespace

ChatEventBridge::ChatEventBridge(presentation::ChatEventSink& chat_ui)
    : chat_ui_(chat_ui) {}

void ChatEventBridge::HandleEvent(chat::ChatEvent event) {
  using yac::chat::ChatEventType;
  using yac::chat::ChatMessageStatus;
  using yac::presentation::MessageStatus;
  using yac::presentation::Sender;
  namespace tool_data = ::yac::tool_call;

  auto& chat_ui = chat_ui_.get();

  switch (event.type) {
    case ChatEventType::UserMessageQueued:
      chat_ui.AddMessageWithId(event.message_id, Sender::User,
                               std::move(event.text), event.status,
                               std::move(event.role_label));
      break;

    case ChatEventType::UserMessageActive:
      chat_ui.SetMessageStatus(event.message_id, event.status);
      break;

    case ChatEventType::Started:
      if (!chat_ui.HasMessage(event.message_id)) {
        chat_ui.StartAgentMessage(event.message_id);
      }
      chat_ui.SetMessageStatus(event.message_id, event.status);
      chat_ui.SetTyping(true);
      yac::presentation::terminal::SetTitle("YAC \xe2\x80\x93 typing...");
      break;

    case ChatEventType::TextDelta:
      chat_ui.AppendToAgentMessage(event.message_id, std::move(event.text));
      break;

    case ChatEventType::Error:
      chat_ui.SetTyping(false);
      chat_ui.SetTransientStatus(
          presentation::UiNotice{.severity = presentation::UiSeverity::Error,
                                 .title = "Provider error",
                                 .detail = event.text});
      if (!chat_ui.HasMessage(event.message_id)) {
        chat_ui.AddMessageWithId(event.message_id, SenderForRole(event.role),
                                 "Error: " + event.text,
                                 ChatMessageStatus::Error);
      } else {
        chat_ui.AppendToAgentMessage(event.message_id, "Error: " + event.text);
        chat_ui.SetMessageStatus(event.message_id, ChatMessageStatus::Error);
      }
      break;

    case ChatEventType::AssistantMessageDone:
      chat_ui.SetTyping(false);
      chat_ui.SetMessageStatus(event.message_id, MessageStatus::Complete);
      yac::presentation::terminal::SetTitle("YAC \xe2\x80\x93 " + event.model);
      yac::presentation::terminal::SendNotification("Response complete");
      break;

    case ChatEventType::Finished:
      chat_ui.SetTyping(false);
      yac::presentation::terminal::SetTitle("YAC \xe2\x80\x93 " +
                                            chat_ui.Model());
      break;

    case ChatEventType::Cancelled:
      chat_ui.SetTyping(false);
      chat_ui.SetMessageStatus(event.message_id, MessageStatus::Cancelled);
      chat_ui.SetTransientStatus(
          presentation::UiNotice{.severity = presentation::UiSeverity::Info,
                                 .title = "Response cancelled"});
      break;

    case ChatEventType::MessageStatusChanged:
      if (event.status == MessageStatus::Cancelled) {
        chat_ui.SetTyping(false);
      }
      chat_ui.SetMessageStatus(event.message_id, event.status);
      break;

    case ChatEventType::ConversationCleared:
      chat_ui.ClearMessages();
      chat_ui.SetTyping(false);
      chat_ui.SetTransientStatus(
          presentation::UiNotice{.severity = presentation::UiSeverity::Info,
                                 .title = "Conversation cleared"});
      break;

    case ChatEventType::ModelChanged:
      chat_ui.SetContextWindowTokens(LookupContextWindow(event.model));
      chat_ui.SetProviderModel(std::move(event.provider_id),
                               std::move(event.model));
      chat_ui.SetTransientStatus(
          presentation::UiNotice{.severity = presentation::UiSeverity::Info,
                                 .title = "Model switched"});
      break;

    case ChatEventType::UsageReported:
      if (event.usage.has_value()) {
        chat_ui.SetLastUsage(presentation::UsageStats{
            event.usage->prompt_tokens, event.usage->completion_tokens,
            event.usage->total_tokens});
      }
      break;

    case ChatEventType::QueueDepthChanged:
      chat_ui.SetQueueDepth(event.queue_depth);
      break;

    case ChatEventType::ToolCallStarted:
      if (event.tool_call.has_value()) {
        chat_ui.AddToolCallMessageWithId(
            event.message_id, std::move(*event.tool_call), event.status);
      }
      break;

    case ChatEventType::ToolCallDone:
      if (event.tool_call.has_value()) {
        chat_ui.UpdateToolCallMessage(
            event.message_id, std::move(*event.tool_call), event.status);
      }
      break;

    case ChatEventType::ToolApprovalRequested:
      chat_ui.ShowToolApproval(
          std::move(event.approval_id), std::move(event.tool_name),
          std::move(event.text), std::move(event.tool_call));
      break;

    case ChatEventType::ToolCallRequested:
      break;

    case ChatEventType::SubAgentProgress: {
      if (event.tool_call.has_value()) {
        chat_ui.UpdateSubAgentToolCallMessage(
            event.message_id, std::move(event.tool_call_id),
            std::move(event.tool_name), std::move(*event.tool_call),
            event.status);
      }
      tool_data::SubAgentCall block{
          .task = event.sub_agent_task,
          .status = tool_data::SubAgentStatus::Running,
          .agent_id = event.sub_agent_id,
          .tool_count = event.sub_agent_tool_count,
      };
      chat_ui.UpdateToolCallMessage(event.message_id, std::move(block),
                                    MessageStatus::Active);
      break;
    }

    case ChatEventType::SubAgentCompleted: {
      tool_data::SubAgentCall block{
          .task = event.sub_agent_task,
          .status = tool_data::SubAgentStatus::Complete,
          .agent_id = event.sub_agent_id,
          .result = event.sub_agent_result,
          .tool_count = event.sub_agent_tool_count,
          .elapsed_ms = event.sub_agent_elapsed_ms,
      };
      chat_ui.UpdateToolCallMessage(event.message_id, std::move(block),
                                    MessageStatus::Complete);
      auto task_short = event.sub_agent_task.substr(0, 40);
      chat_ui.SetTransientStatus(presentation::UiNotice{
          .severity = presentation::UiSeverity::Info,
          .title = "Sub-agent completed",
          .detail = task_short,
      });
      break;
    }

    case ChatEventType::SubAgentError: {
      tool_data::SubAgentCall block{
          .task = event.sub_agent_task,
          .status = tool_data::SubAgentStatus::Error,
          .agent_id = event.sub_agent_id,
          .result = event.sub_agent_result,
          .tool_count = event.sub_agent_tool_count,
          .elapsed_ms = event.sub_agent_elapsed_ms,
      };
      chat_ui.UpdateToolCallMessage(event.message_id, std::move(block),
                                    MessageStatus::Error);
      break;
    }

    case ChatEventType::SubAgentCancelled: {
      tool_data::SubAgentCall block{
          .task = event.sub_agent_task,
          .status = tool_data::SubAgentStatus::Cancelled,
          .agent_id = event.sub_agent_id,
      };
      chat_ui.UpdateToolCallMessage(event.message_id, std::move(block),
                                    MessageStatus::Cancelled);
      break;
    }
  }
}

}  // namespace yac::app
