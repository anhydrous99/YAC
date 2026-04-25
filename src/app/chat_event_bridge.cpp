#include "app/chat_event_bridge.hpp"

#include "app/model_context_windows.hpp"
#include "chat/tool_call_argument_parser.hpp"
#include "core_types/tool_call_types.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/chat_ui_overlay_state.hpp"
#include "presentation/util/terminal.hpp"

#include <algorithm>
#include <utility>
#include <variant>

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
    case ChatRole::Tool:
      return Sender::Agent;
  }
  return Sender::Agent;
}

}  // namespace

ChatEventBridge::ChatEventBridge(presentation::ChatEventSink& chat_ui,
                                 HistoryProvider history_provider)
    : chat_ui_(chat_ui), history_provider_(std::move(history_provider)) {}

void ChatEventBridge::HandleEvent(chat::ChatEvent event) {
  std::visit([this](auto& payload) { Handle(std::move(payload)); },
             event.payload);
}

void ChatEventBridge::Handle(chat::UserMessageQueuedEvent event) {
  chat_ui_.get().AddMessageWithId(
      event.message_id, yac::presentation::Sender::User, std::move(event.text),
      event.status, std::move(event.role_label));
}

void ChatEventBridge::Handle(chat::UserMessageActiveEvent event) {
  chat_ui_.get().SetMessageStatus(event.message_id, event.status);
}

void ChatEventBridge::Handle(chat::StartedEvent event) {
  auto& chat_ui = chat_ui_.get();
  if (!chat_ui.HasMessage(event.message_id)) {
    chat_ui.StartAgentMessage(event.message_id);
  }
  chat_ui.SetMessageStatus(event.message_id, event.status);
  chat_ui.SetTyping(true);
  yac::presentation::terminal::SetTitle("YAC \xe2\x80\x93 typing...");
}

void ChatEventBridge::Handle(chat::TextDeltaEvent event) {
  chat_ui_.get().AppendToAgentMessage(event.message_id, std::move(event.text));
}

void ChatEventBridge::Handle(chat::ErrorEvent event) {
  using yac::chat::ChatMessageStatus;
  auto& chat_ui = chat_ui_.get();
  chat_ui.SetTyping(false);
  chat_ui.SetTransientStatus(presentation::UiNotice{
      .severity = presentation::UiSeverity::Error,
      .title = event.provider_id.empty() ? "YAC error" : "Provider error",
      .detail = event.text});
  if (!chat_ui.HasMessage(event.message_id)) {
    chat_ui.AddMessageWithId(event.message_id, SenderForRole(event.role),
                             "Error: " + event.text, ChatMessageStatus::Error);
    return;
  }
  chat_ui.AppendToAgentMessage(event.message_id, "Error: " + event.text);
  chat_ui.SetMessageStatus(event.message_id, ChatMessageStatus::Error);
}

void ChatEventBridge::Handle(chat::AssistantMessageDoneEvent event) {
  using yac::presentation::MessageStatus;
  chat_ui_.get().SetTyping(false);
  chat_ui_.get().SetMessageStatus(event.message_id, MessageStatus::Complete);
  yac::presentation::terminal::SetTitle("YAC \xe2\x80\x93 " + event.model);
  yac::presentation::terminal::SendNotification("Response complete");
}

void ChatEventBridge::Handle(chat::FinishedEvent event) {
  (void)event;
  auto& chat_ui = chat_ui_.get();
  chat_ui.SetTyping(false);
  yac::presentation::terminal::SetTitle("YAC \xe2\x80\x93 " + chat_ui.Model());
}

void ChatEventBridge::Handle(chat::CancelledEvent event) {
  using yac::presentation::MessageStatus;
  auto& chat_ui = chat_ui_.get();
  chat_ui.SetTyping(false);
  chat_ui.SetMessageStatus(event.message_id, MessageStatus::Cancelled);
  chat_ui.SetTransientStatus(
      presentation::UiNotice{.severity = presentation::UiSeverity::Info,
                             .title = "Response cancelled"});
}

void ChatEventBridge::Handle(chat::MessageStatusChangedEvent event) {
  using yac::presentation::MessageStatus;
  auto& chat_ui = chat_ui_.get();
  if (event.status == MessageStatus::Cancelled) {
    chat_ui.SetTyping(false);
  }
  chat_ui.SetMessageStatus(event.message_id, event.status);
}

void ChatEventBridge::Handle(chat::ConversationClearedEvent event) {
  (void)event;
  auto& chat_ui = chat_ui_.get();
  chat_ui.ClearMessages();
  chat_ui.SetTyping(false);
  chat_ui.SetTransientStatus(
      presentation::UiNotice{.severity = presentation::UiSeverity::Info,
                             .title = "Conversation cleared"});
}

void ChatEventBridge::RefreshFromHistory() {
  auto history = history_provider_ ? history_provider_()
                                   : std::vector<chat::ChatMessage>{};
  auto& chat_ui = chat_ui_.get();
  chat_ui.ClearMessages();
  for (const auto& message : history) {
    // Tool result entries are part of the prior assistant turn's context;
    // after compaction they have no structured ToolCallBlock to render, so
    // we skip them here. Live tool segments are restored by the normal
    // event stream.
    if (message.role == chat::ChatRole::Tool) {
      continue;
    }
    const auto sender = SenderForRole(message.role);
    chat_ui.AddMessageWithId(message.id, sender, message.content,
                             message.status);
  }
  chat_ui.SetTyping(false);
}

void ChatEventBridge::Handle(chat::ConversationCompactedEvent event) {
  (void)event;
  RefreshFromHistory();
  chat_ui_.get().SetTransientStatus(
      presentation::UiNotice{.severity = presentation::UiSeverity::Info,
                             .title = "Conversation compacted"});
}

void ChatEventBridge::Handle(chat::ModelChangedEvent event) {
  auto& chat_ui = chat_ui_.get();
  chat_ui.SetContextWindowTokens(LookupContextWindow(event.model));
  chat_ui.SetProviderModel(std::move(event.provider_id),
                           std::move(event.model));
  chat_ui.SetTransientStatus(presentation::UiNotice{
      .severity = presentation::UiSeverity::Info, .title = "Model switched"});
}

void ChatEventBridge::Handle(chat::AgentModeChangedEvent event) {
  if (auto* ui = dynamic_cast<presentation::ChatUI*>(&chat_ui_.get())) {
    ui->SetAgentMode(event.mode);
  }
}

void ChatEventBridge::Handle(chat::UsageReportedEvent event) {
  chat_ui_.get().SetLastUsage(presentation::UsageStats{
      event.usage.prompt_tokens, event.usage.completion_tokens,
      event.usage.total_tokens});
}

void ChatEventBridge::Handle(chat::QueueDepthChangedEvent event) {
  chat_ui_.get().SetQueueDepth(event.queue_depth);
}

void ChatEventBridge::Handle(chat::ToolCallStartedEvent event) {
  auto& chat_ui = chat_ui_.get();
  if (chat_ui.HasToolSegment(event.message_id)) {
    chat_ui.UpdateToolCallMessage(event.message_id, std::move(event.tool_call),
                                  event.status);
  } else {
    chat_ui.AddToolCallSegment(event.message_id, std::move(event.tool_call),
                               event.status);
  }
}

void ChatEventBridge::Handle(chat::ToolCallDoneEvent event) {
  chat_ui_.get().UpdateToolCallMessage(
      event.message_id, std::move(event.tool_call), event.status);
}

void ChatEventBridge::Handle(chat::ToolCallArgumentDeltaEvent event) {
  namespace tool_data = ::yac::tool_call;

  if (event.card_message_id == 0 || event.tool_call_id.empty()) {
    return;
  }
  if (!event.tool_name.empty() &&
      event.tool_name != tool_data::kFileWriteToolName) {
    return;
  }

  const auto filepath =
      chat::ExtractStringFieldPartial(event.arguments_json, "filepath")
          .value_or(std::string{});
  const auto content =
      chat::ExtractStringFieldPartial(event.arguments_json, "content")
          .value_or(std::string{});
  const auto line_count =
      static_cast<int>(std::count(content.begin(), content.end(), '\n') +
                       (content.empty() ? 0 : 1));

  tool_data::FileWriteCall block{
      .filepath = filepath,
      .content_preview = content,
      .lines_added = line_count,
      .is_streaming = true,
  };

  auto& chat_ui = chat_ui_.get();
  if (chat_ui.HasToolSegment(event.card_message_id)) {
    chat_ui.UpdateToolCallMessage(event.card_message_id, std::move(block),
                                  yac::presentation::MessageStatus::Active);
  } else {
    chat_ui.AddToolCallSegment(event.card_message_id, std::move(block),
                               yac::presentation::MessageStatus::Active);
  }
}

void ChatEventBridge::Handle(chat::ToolApprovalRequestedEvent event) {
  namespace tool_data = ::yac::tool_call;
  auto* ask_user = std::get_if<tool_data::AskUserCall>(&event.tool_call);
  if (ask_user != nullptr) {
    chat_ui_.get().ShowAskUserDialog(std::move(event.approval_id),
                                     std::move(event.question),
                                     std::move(event.options));
    return;
  }
  chat_ui_.get().ShowToolApproval(
      std::move(event.approval_id), std::move(event.tool_name),
      std::move(event.text), std::move(event.tool_call));
}

void ChatEventBridge::Handle(chat::ToolCallRequestedEvent event) {
  (void)event;
}

void ChatEventBridge::Handle(chat::SubAgentProgressEvent event) {
  using yac::presentation::MessageStatus;
  namespace tool_data = ::yac::tool_call;

  auto& chat_ui = chat_ui_.get();
  if (event.child_tool.has_value()) {
    auto& child_tool = *event.child_tool;
    chat_ui.UpdateSubAgentToolCallMessage(
        event.message_id, std::move(child_tool.tool_call_id),
        std::move(child_tool.tool_name), std::move(child_tool.tool_call),
        child_tool.status);
  }
  tool_data::SubAgentCall block{
      .task = event.sub_agent_task,
      .status = tool_data::SubAgentStatus::Running,
      .agent_id = event.sub_agent_id,
      .tool_count = event.sub_agent_tool_count,
  };
  chat_ui.UpdateToolCallMessage(event.message_id, std::move(block),
                                MessageStatus::Active);
}

void ChatEventBridge::Handle(chat::SubAgentCompletedEvent event) {
  using yac::presentation::MessageStatus;
  namespace tool_data = ::yac::tool_call;

  tool_data::SubAgentCall block{
      .task = event.sub_agent_task,
      .status = tool_data::SubAgentStatus::Complete,
      .agent_id = event.sub_agent_id,
      .result = event.sub_agent_result,
      .tool_count = event.sub_agent_tool_count,
      .elapsed_ms = event.sub_agent_elapsed_ms,
  };
  auto& chat_ui = chat_ui_.get();
  chat_ui.UpdateToolCallMessage(event.message_id, std::move(block),
                                MessageStatus::Complete);
  auto task_short = event.sub_agent_task.substr(0, 40);
  chat_ui.SetTransientStatus(presentation::UiNotice{
      .severity = presentation::UiSeverity::Info,
      .title = "Sub-agent completed",
      .detail = task_short,
  });
}

void ChatEventBridge::Handle(chat::SubAgentErrorEvent event) {
  using yac::presentation::MessageStatus;
  namespace tool_data = ::yac::tool_call;

  tool_data::SubAgentCall block{
      .task = event.sub_agent_task,
      .status = tool_data::SubAgentStatus::Error,
      .agent_id = event.sub_agent_id,
      .result = event.sub_agent_result,
      .tool_count = event.sub_agent_tool_count,
      .elapsed_ms = event.sub_agent_elapsed_ms,
  };
  chat_ui_.get().UpdateToolCallMessage(event.message_id, std::move(block),
                                       MessageStatus::Error);
}

void ChatEventBridge::Handle(chat::SubAgentCancelledEvent event) {
  using yac::presentation::MessageStatus;
  namespace tool_data = ::yac::tool_call;

  tool_data::SubAgentCall block{
      .task = event.sub_agent_task,
      .status = tool_data::SubAgentStatus::Cancelled,
      .agent_id = event.sub_agent_id,
  };
  chat_ui_.get().UpdateToolCallMessage(event.message_id, std::move(block),
                                       MessageStatus::Cancelled);
}

}  // namespace yac::app
