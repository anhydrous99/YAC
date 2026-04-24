#pragma once

#include "chat/types.hpp"
#include "presentation/chat_event_sink.hpp"

#include <functional>
#include <vector>

namespace yac::app {

class ChatEventBridge {
 public:
  using HistoryProvider = std::function<std::vector<chat::ChatMessage>()>;

  ChatEventBridge(presentation::ChatEventSink& chat_ui,
                  HistoryProvider history_provider = {});

  void HandleEvent(chat::ChatEvent event);

 private:
  void Handle(chat::StartedEvent event);
  void Handle(chat::TextDeltaEvent event);
  void Handle(chat::AssistantMessageDoneEvent event);
  void Handle(chat::ToolCallStartedEvent event);
  void Handle(chat::ToolCallDoneEvent event);
  void Handle(chat::ErrorEvent event);
  void Handle(chat::FinishedEvent event);
  void Handle(chat::CancelledEvent event);
  void Handle(chat::UserMessageQueuedEvent event);
  void Handle(chat::UserMessageActiveEvent event);
  void Handle(chat::MessageStatusChangedEvent event);
  void Handle(chat::QueueDepthChangedEvent event);
  void Handle(chat::ConversationClearedEvent event);
  void Handle(chat::ConversationCompactedEvent event);
  void Handle(chat::ModelChangedEvent event);
  void Handle(chat::AgentModeChangedEvent event);
  void Handle(chat::ToolCallRequestedEvent event);
  void Handle(chat::ToolCallArgumentDeltaEvent event);
  void Handle(chat::ToolApprovalRequestedEvent event);
  void Handle(chat::UsageReportedEvent event);
  void Handle(chat::SubAgentProgressEvent event);
  void Handle(chat::SubAgentCompletedEvent event);
  void Handle(chat::SubAgentErrorEvent event);
  void Handle(chat::SubAgentCancelledEvent event);
  void RefreshFromHistory();

  std::reference_wrapper<presentation::ChatEventSink> chat_ui_;
  HistoryProvider history_provider_;
};

}  // namespace yac::app
