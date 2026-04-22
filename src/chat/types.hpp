#pragma once

#include "core_types/chat_ids.hpp"
#include "core_types/tool_call_types.hpp"
#include "model_info.hpp"

#include <chrono>
#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace yac::chat {

struct ChatMessage {
  ChatMessageId id = 0;
  ChatRole role = ChatRole::User;
  ChatMessageStatus status = ChatMessageStatus::Complete;
  std::string content;
  std::vector<ToolCallRequest> tool_calls;
  std::string tool_call_id;
  std::string tool_name;
};

struct ChatRequest {
  std::string provider_id = "openai";
  std::string model = "gpt-4o-mini";
  std::vector<ChatMessage> messages;
  std::vector<ToolDefinition> tools;
  double temperature = 0.7;
  bool stream = true;
};

enum class ChatEventType {
  Started,
  TextDelta,
  AssistantMessageDone,
  ToolCallStarted,
  ToolCallDone,
  Error,
  Finished,
  Cancelled,
  UserMessageQueued,
  UserMessageActive,
  MessageStatusChanged,
  QueueDepthChanged,
  ConversationCleared,
  ModelChanged,
  ToolCallRequested,
  ToolCallArgumentDelta,
  ToolApprovalRequested,
  UsageReported,
  SubAgentProgress,
  SubAgentCompleted,
  SubAgentError,
  SubAgentCancelled,
};

struct StartedEvent {
  static constexpr ChatEventType kType = ChatEventType::Started;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  std::string provider_id;
  std::string model;
  ChatMessageStatus status = ChatMessageStatus::Active;
};

struct TextDeltaEvent {
  static constexpr ChatEventType kType = ChatEventType::TextDelta;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  std::string text;
  std::string provider_id;
  std::string model;
};

struct AssistantMessageDoneEvent {
  static constexpr ChatEventType kType = ChatEventType::AssistantMessageDone;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  std::string provider_id;
  std::string model;
  ChatMessageStatus status = ChatMessageStatus::Complete;
};

struct ToolCallStartedEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolCallStarted;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Tool;
  std::string tool_call_id;
  std::string tool_name;
  ::yac::tool_call::ToolCallBlock tool_call;
  ChatMessageStatus status = ChatMessageStatus::Active;
};

struct ToolCallDoneEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolCallDone;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Tool;
  std::string tool_call_id;
  std::string tool_name;
  ::yac::tool_call::ToolCallBlock tool_call;
  ChatMessageStatus status = ChatMessageStatus::Complete;
};

struct ErrorEvent {
  static constexpr ChatEventType kType = ChatEventType::Error;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  std::string text;
  std::string provider_id;
  std::string model;
  ChatMessageStatus status = ChatMessageStatus::Error;
};

struct FinishedEvent {
  static constexpr ChatEventType kType = ChatEventType::Finished;

  ChatMessageId message_id = 0;
};

struct CancelledEvent {
  static constexpr ChatEventType kType = ChatEventType::Cancelled;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  ChatMessageStatus status = ChatMessageStatus::Cancelled;
};

struct UserMessageQueuedEvent {
  static constexpr ChatEventType kType = ChatEventType::UserMessageQueued;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::User;
  std::string text;
  ChatMessageStatus status = ChatMessageStatus::Queued;
  std::string role_label;
};

struct UserMessageActiveEvent {
  static constexpr ChatEventType kType = ChatEventType::UserMessageActive;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::User;
  ChatMessageStatus status = ChatMessageStatus::Active;
};

struct MessageStatusChangedEvent {
  static constexpr ChatEventType kType = ChatEventType::MessageStatusChanged;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  ChatMessageStatus status = ChatMessageStatus::Complete;
};

struct QueueDepthChangedEvent {
  static constexpr ChatEventType kType = ChatEventType::QueueDepthChanged;

  int queue_depth = 0;
};

struct ConversationClearedEvent {
  static constexpr ChatEventType kType = ChatEventType::ConversationCleared;
};

struct ModelChangedEvent {
  static constexpr ChatEventType kType = ChatEventType::ModelChanged;

  std::string provider_id;
  std::string model;
};

struct ToolCallRequestedEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolCallRequested;

  std::vector<ToolCallRequest> tool_calls;
};

struct ToolCallArgumentDeltaEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolCallArgumentDelta;

  ChatMessageId message_id = 0;
  ChatMessageId card_message_id = 0;
  std::string tool_call_id;
  std::string tool_name;
  std::string arguments_json;
};

struct ToolApprovalRequestedEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolApprovalRequested;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Tool;
  std::string text;
  std::string tool_call_id;
  std::string tool_name;
  std::string approval_id;
  ::yac::tool_call::ToolCallBlock tool_call;
  ChatMessageStatus status = ChatMessageStatus::Queued;
};

struct UsageReportedEvent {
  static constexpr ChatEventType kType = ChatEventType::UsageReported;

  std::string provider_id;
  std::string model;
  TokenUsage usage;
};

struct SubAgentChildToolEvent {
  std::string tool_call_id;
  std::string tool_name;
  ::yac::tool_call::ToolCallBlock tool_call;
  ChatMessageStatus status = ChatMessageStatus::Complete;
};

struct SubAgentProgressEvent {
  static constexpr ChatEventType kType = ChatEventType::SubAgentProgress;

  ChatMessageId message_id = 0;
  std::string sub_agent_id;
  std::string sub_agent_task;
  int sub_agent_tool_count = 0;
  ChatMessageStatus status = ChatMessageStatus::Active;
  std::optional<SubAgentChildToolEvent> child_tool;
};

struct SubAgentCompletedEvent {
  static constexpr ChatEventType kType = ChatEventType::SubAgentCompleted;

  ChatMessageId message_id = 0;
  std::string sub_agent_id;
  std::string sub_agent_task;
  std::string sub_agent_result;
  int sub_agent_tool_count = 0;
  int sub_agent_elapsed_ms = 0;
};

struct SubAgentErrorEvent {
  static constexpr ChatEventType kType = ChatEventType::SubAgentError;

  ChatMessageId message_id = 0;
  std::string sub_agent_id;
  std::string sub_agent_task;
  std::string sub_agent_result;
  int sub_agent_tool_count = 0;
  int sub_agent_elapsed_ms = 0;
};

struct SubAgentCancelledEvent {
  static constexpr ChatEventType kType = ChatEventType::SubAgentCancelled;

  ChatMessageId message_id = 0;
  std::string sub_agent_id;
  std::string sub_agent_task;
};

struct ChatEvent {
  using Payload = std::variant<
      StartedEvent, TextDeltaEvent, AssistantMessageDoneEvent,
      ToolCallStartedEvent, ToolCallDoneEvent, ErrorEvent, FinishedEvent,
      CancelledEvent, UserMessageQueuedEvent, UserMessageActiveEvent,
      MessageStatusChangedEvent, QueueDepthChangedEvent,
      ConversationClearedEvent, ModelChangedEvent, ToolCallRequestedEvent,
      ToolCallArgumentDeltaEvent, ToolApprovalRequestedEvent,
      UsageReportedEvent, SubAgentProgressEvent, SubAgentCompletedEvent,
      SubAgentErrorEvent, SubAgentCancelledEvent>;

  ChatEvent() = default;

  template <typename Event, typename = std::enable_if_t<!std::is_same_v<
                                std::decay_t<Event>, ChatEvent>>>
  ChatEvent(Event&& event) : payload(std::forward<Event>(event)) {}

  [[nodiscard]] ChatEventType Type() const {
    return std::visit(
        [](const auto& event) {
          using Event = std::decay_t<decltype(event)>;
          return Event::kType;
        },
        payload);
  }

  template <typename Event>
  [[nodiscard]] Event* As() {
    return std::get_if<Event>(&payload);
  }

  template <typename Event>
  [[nodiscard]] const Event* As() const {
    return std::get_if<Event>(&payload);
  }

  template <typename Event>
  [[nodiscard]] Event& Get() {
    return std::get<Event>(payload);
  }

  template <typename Event>
  [[nodiscard]] const Event& Get() const {
    return std::get<Event>(payload);
  }

  Payload payload;
};

struct ProviderConfig {
  std::string id = "openai";
  std::string model = "gpt-4o-mini";
  std::string api_key;
  std::string api_key_env = "OPENAI_API_KEY";
  std::string base_url = "https://api.openai.com/v1/";
  std::optional<std::string> system_prompt;
  std::map<std::string, std::string> options;
};

enum class ConfigIssueSeverity { Info, Warning, Error };

struct ConfigIssue {
  ConfigIssueSeverity severity = ConfigIssueSeverity::Info;
  std::string message;
  std::string detail;
};

struct ChatConfig {
  std::string provider_id = "openai";
  std::string model = "gpt-4o-mini";
  std::string base_url = "https://api.openai.com/v1/";
  double temperature = 0.7;
  std::string api_key;
  std::string api_key_env = "OPENAI_API_KEY";
  std::string workspace_root;
  std::string lsp_clangd_command = "clangd";
  std::vector<std::string> lsp_clangd_args;
  std::optional<std::string> system_prompt;
};

struct ChatConfigResult {
  ChatConfig config;
  std::vector<ConfigIssue> issues;
};

}  // namespace yac::chat
