#include "app/chat_event_bridge.hpp"

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

ChatEventBridge::ChatEventBridge(presentation::ChatUI& chat_ui)
    : chat_ui_(&chat_ui) {}

void ChatEventBridge::HandleEvent(chat::ChatEvent event) {
  using yac::chat::ChatEventType;
  using yac::chat::ChatMessageStatus;
  using yac::presentation::MessageStatus;
  using yac::presentation::Sender;

  switch (event.type) {
    case ChatEventType::UserMessageQueued:
      chat_ui_->AddMessageWithId(event.message_id, Sender::User,
                                 std::move(event.text), event.status);
      break;

    case ChatEventType::UserMessageActive:
      chat_ui_->SetMessageStatus(event.message_id, event.status);
      break;

    case ChatEventType::Started:
      if (!chat_ui_->HasMessage(event.message_id)) {
        chat_ui_->StartAgentMessage(event.message_id);
      }
      chat_ui_->SetMessageStatus(event.message_id, event.status);
      chat_ui_->SetTyping(true);
      yac::presentation::terminal::SetTitle("YAC \xe2\x80\x93 typing...");
      break;

    case ChatEventType::TextDelta:
      chat_ui_->AppendToAgentMessage(event.message_id, std::move(event.text));
      break;

    case ChatEventType::Error:
      chat_ui_->SetTyping(false);
      if (!chat_ui_->HasMessage(event.message_id)) {
        chat_ui_->AddMessageWithId(event.message_id, SenderForRole(event.role),
                                   "Error: " + event.text,
                                   ChatMessageStatus::Error);
      } else {
        chat_ui_->AppendToAgentMessage(event.message_id,
                                       "Error: " + event.text);
        chat_ui_->SetMessageStatus(event.message_id, ChatMessageStatus::Error);
      }
      break;

    case ChatEventType::AssistantMessageDone:
      chat_ui_->SetTyping(false);
      chat_ui_->SetMessageStatus(event.message_id, MessageStatus::Complete);
      yac::presentation::terminal::SetTitle("YAC \xe2\x80\x93 " + event.model);
      yac::presentation::terminal::SendNotification("Response complete");
      break;

    case ChatEventType::Finished:
      chat_ui_->SetTyping(false);
      yac::presentation::terminal::SetTitle("YAC \xe2\x80\x93 " +
                                            chat_ui_->Model());
      break;

    case ChatEventType::Cancelled:
      chat_ui_->SetTyping(false);
      chat_ui_->SetMessageStatus(event.message_id, MessageStatus::Cancelled);
      break;

    case ChatEventType::MessageStatusChanged:
      if (event.status == MessageStatus::Cancelled) {
        chat_ui_->SetTyping(false);
      }
      chat_ui_->SetMessageStatus(event.message_id, event.status);
      break;

    case ChatEventType::ConversationCleared:
      chat_ui_->ClearMessages();
      chat_ui_->SetTyping(false);
      break;

    case ChatEventType::ModelChanged:
      chat_ui_->SetProviderModel(std::move(event.provider_id),
                                 std::move(event.model));
      break;

    case ChatEventType::QueueDepthChanged:
      break;

    case ChatEventType::ToolCallStarted:
      if (event.tool_call.has_value()) {
        chat_ui_->AddToolCallMessageWithId(
            event.message_id, std::move(*event.tool_call), event.status);
      }
      break;

    case ChatEventType::ToolCallDone:
      if (event.tool_call.has_value()) {
        chat_ui_->UpdateToolCallMessage(
            event.message_id, std::move(*event.tool_call), event.status);
      }
      break;

    case ChatEventType::ToolApprovalRequested:
      chat_ui_->ShowToolApproval(std::move(event.approval_id),
                                 std::move(event.tool_name),
                                 std::move(event.text));
      break;

    case ChatEventType::ToolCallRequested:
      break;
  }
}

}  // namespace yac::app
