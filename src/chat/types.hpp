#pragma once

#include "core_types/agent_mode.hpp"
#include "core_types/chat_ids.hpp"
#include "core_types/tool_call_types.hpp"
#include "core_types/typed_ids.hpp"
#include "mcp/mcp_server_config.hpp"
#include "model_info.hpp"

#include <map>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace yac::chat {

static_assert(sizeof(ModelInfo) > 0);

inline constexpr int kDefaultToolRoundLimit = 64;
inline constexpr int kMinToolRoundLimit = 1;
inline constexpr int kMaxToolRoundLimit = 256;

inline constexpr int kMinContextWindow = 1;
inline constexpr int kMaxContextWindow = 10'000'000;

inline constexpr double kMinTemperature = 0.0;
inline constexpr double kMaxTemperature = 2.0;

inline constexpr double kMinAutoCompactThreshold = 0.05;
inline constexpr double kMaxAutoCompactThreshold = 1.0;
inline constexpr int kMinAutoCompactKeepLast = 1;
inline constexpr int kMaxAutoCompactKeepLast = 10000;

struct ChatMessage {
  ChatMessageId id = 0;
  ChatRole role = ChatRole::User;
  ChatMessageStatus status = ChatMessageStatus::Complete;
  std::string content;
  std::vector<ToolCallRequest> tool_calls;
  ToolCallId tool_call_id;
  std::string tool_name;
};

struct ChatRequest {
  ProviderId provider_id{"openai-compatible"};
  ModelId model{"gpt-4o-mini"};
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
  ConversationCompacted,
  ModelChanged,
  ToolCallRequested,
  ToolCallArgumentDelta,
  ToolApprovalRequested,
  UsageReported,
  SubAgentProgress,
  SubAgentCompleted,
  SubAgentError,
  SubAgentCancelled,
  AgentModeChanged,
  McpServerStateChanged,
  McpAuthRequired,
  McpProgressUpdate,
};

struct StartedEvent {
  static constexpr ChatEventType kType = ChatEventType::Started;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  ProviderId provider_id;
  ModelId model;
  ChatMessageStatus status = ChatMessageStatus::Active;
};

struct TextDeltaEvent {
  static constexpr ChatEventType kType = ChatEventType::TextDelta;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  std::string text;
  ProviderId provider_id;
  ModelId model;
};

struct AssistantMessageDoneEvent {
  static constexpr ChatEventType kType = ChatEventType::AssistantMessageDone;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  ProviderId provider_id;
  ModelId model;
  ChatMessageStatus status = ChatMessageStatus::Complete;
};

struct ToolCallStartedEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolCallStarted;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Tool;
  ToolCallId tool_call_id;
  std::string tool_name;
  ::yac::tool_call::ToolCallBlock tool_call;
  ChatMessageStatus status = ChatMessageStatus::Active;
};

struct ToolCallDoneEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolCallDone;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Tool;
  ToolCallId tool_call_id;
  std::string tool_name;
  ::yac::tool_call::ToolCallBlock tool_call;
  ChatMessageStatus status = ChatMessageStatus::Complete;
};

struct ErrorEvent {
  static constexpr ChatEventType kType = ChatEventType::Error;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  std::string text;
  ProviderId provider_id;
  ModelId model;
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

enum class CompactReason { Manual, Auto };

struct ConversationCompactedEvent {
  static constexpr ChatEventType kType = ChatEventType::ConversationCompacted;

  CompactReason reason = CompactReason::Manual;
  // Number of non-system messages folded into the synthetic note. 0 when the
  // event is constructed without a count (older callers).
  int messages_removed = 0;
};

struct ModelChangedEvent {
  static constexpr ChatEventType kType = ChatEventType::ModelChanged;

  ProviderId provider_id;
  ModelId model;
};

struct ToolCallRequestedEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolCallRequested;

  std::vector<ToolCallRequest> tool_calls;
};

struct ToolCallArgumentDeltaEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolCallArgumentDelta;

  ChatMessageId message_id = 0;
  ChatMessageId card_message_id = 0;
  ToolCallId tool_call_id;
  std::string tool_name;
  std::string arguments_json;
};

struct ToolApprovalRequestedEvent {
  static constexpr ChatEventType kType = ChatEventType::ToolApprovalRequested;

  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Tool;
  std::string text;
  ToolCallId tool_call_id;
  std::string tool_name;
  ApprovalId approval_id;
  ::yac::tool_call::ToolCallBlock tool_call;
  ChatMessageStatus status = ChatMessageStatus::Queued;
  std::string question;
  std::vector<std::string> options;
};

struct UsageReportedEvent {
  static constexpr ChatEventType kType = ChatEventType::UsageReported;

  ProviderId provider_id;
  ModelId model;
  TokenUsage usage;
};

struct SubAgentChildToolEvent {
  ToolCallId tool_call_id;
  std::string tool_name;
  ::yac::tool_call::ToolCallBlock tool_call;
  ChatMessageStatus status = ChatMessageStatus::Complete;
};

struct SubAgentProgressEvent {
  static constexpr ChatEventType kType = ChatEventType::SubAgentProgress;

  ChatMessageId message_id = 0;
  SubAgentId sub_agent_id;
  std::string sub_agent_task;
  int sub_agent_tool_count = 0;
  ChatMessageStatus status = ChatMessageStatus::Active;
  std::optional<SubAgentChildToolEvent> child_tool;
};

struct SubAgentCompletedEvent {
  static constexpr ChatEventType kType = ChatEventType::SubAgentCompleted;

  ChatMessageId message_id = 0;
  SubAgentId sub_agent_id;
  std::string sub_agent_task;
  std::string sub_agent_result;
  int sub_agent_tool_count = 0;
  int sub_agent_elapsed_ms = 0;
};

struct SubAgentErrorEvent {
  static constexpr ChatEventType kType = ChatEventType::SubAgentError;

  ChatMessageId message_id = 0;
  SubAgentId sub_agent_id;
  std::string sub_agent_task;
  std::string sub_agent_result;
  int sub_agent_tool_count = 0;
  int sub_agent_elapsed_ms = 0;
};

struct SubAgentCancelledEvent {
  static constexpr ChatEventType kType = ChatEventType::SubAgentCancelled;

  ChatMessageId message_id = 0;
  SubAgentId sub_agent_id;
  std::string sub_agent_task;
};

struct AgentModeChangedEvent {
  static constexpr ChatEventType kType = ChatEventType::AgentModeChanged;

  AgentMode mode = AgentMode::Build;
};

struct McpServerStateChangedEvent {
  static constexpr ChatEventType kType = ChatEventType::McpServerStateChanged;

  McpServerId server_id;
  std::string state;
  std::string error;
};

struct McpAuthRequiredEvent {
  static constexpr ChatEventType kType = ChatEventType::McpAuthRequired;

  McpServerId server_id;
  std::string hint_message;
};

struct McpProgressUpdateEvent {
  static constexpr ChatEventType kType = ChatEventType::McpProgressUpdate;

  ChatMessageId message_id = 0;
  std::string text;
  double progress = 0.0;
  double total = 0.0;
};

struct ChatEvent {
  using Payload = std::variant<
      StartedEvent, TextDeltaEvent, AssistantMessageDoneEvent,
      ToolCallStartedEvent, ToolCallDoneEvent, ErrorEvent, FinishedEvent,
      CancelledEvent, UserMessageQueuedEvent, UserMessageActiveEvent,
      MessageStatusChangedEvent, QueueDepthChangedEvent,
      ConversationClearedEvent, ConversationCompactedEvent, ModelChangedEvent,
      ToolCallRequestedEvent, ToolCallArgumentDeltaEvent,
      ToolApprovalRequestedEvent, UsageReportedEvent, SubAgentProgressEvent,
      SubAgentCompletedEvent, SubAgentErrorEvent, SubAgentCancelledEvent,
      AgentModeChangedEvent, McpServerStateChangedEvent, McpAuthRequiredEvent,
      McpProgressUpdateEvent>;

  ChatEvent() = default;

  template <typename Event, typename = std::enable_if_t<!std::is_same_v<
                                std::decay_t<Event>, ChatEvent>>>
  explicit ChatEvent(Event&& event) : payload(std::forward<Event>(event)) {}

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

[[nodiscard]] inline ChatEvent MakeMcpServerStateChangedEvent(
    McpServerId server_id, std::string state, std::string error) {
  return ChatEvent{McpServerStateChangedEvent{.server_id = std::move(server_id),
                                              .state = std::move(state),
                                              .error = std::move(error)}};
}

[[nodiscard]] inline ChatEvent MakeMcpAuthRequiredEvent(
    McpServerId server_id, std::string hint_message) {
  return ChatEvent{
      McpAuthRequiredEvent{.server_id = std::move(server_id),
                           .hint_message = std::move(hint_message)}};
}

[[nodiscard]] inline ChatEvent MakeMcpProgressUpdateEvent(
    ChatMessageId message_id, std::string text, double progress, double total) {
  return ChatEvent{McpProgressUpdateEvent{.message_id = message_id,
                                          .text = std::move(text),
                                          .progress = progress,
                                          .total = total}};
}

struct ProviderConfig {
  ProviderId id{"openai-compatible"};
  ModelId model{"gpt-4o-mini"};
  std::string api_key;
  std::string api_key_env = "OPENAI_API_KEY";
  std::string base_url = "https://api.openai.com/v1/";
  std::optional<std::string> system_prompt;
  std::map<std::string, std::string> options;
  int context_window = 0;
};

enum class ConfigIssueSeverity { Info, Warning, Error };

struct ConfigIssue {
  ConfigIssueSeverity severity = ConfigIssueSeverity::Info;
  std::string message;
  std::string detail;
};

struct ChatConfig {
  ProviderId provider_id{"openai-compatible"};
  ModelId model{"gpt-4o-mini"};
  std::string base_url = "https://api.openai.com/v1/";
  double temperature = 0.7;
  std::string api_key;
  std::string api_key_env = "OPENAI_API_KEY";
  std::string workspace_root;
  std::string lsp_clangd_command = "clangd";
  std::vector<std::string> lsp_clangd_args;
  int max_tool_rounds = kDefaultToolRoundLimit;
  std::optional<std::string> system_prompt;
  AgentMode agent_mode{AgentMode::Build};
  bool sync_terminal_background = true;
  std::string theme_name = "vivid";
  std::string theme_density = "comfortable";
  // Auto-compaction settings — fired before each new user prompt when the
  // previous round's prompt_tokens / context_window crosses the threshold.
  bool auto_compact_enabled = true;
  double auto_compact_threshold = 0.8;
  int auto_compact_keep_last = 20;
  std::string auto_compact_mode = "summarize";  // "summarize" | "truncate"
  int context_window = 0;
  std::map<std::string, std::string> options;
  mcp::McpConfig mcp;
};

struct ChatConfigResult {
  ChatConfig config;
  std::vector<ConfigIssue> issues;
};

}  // namespace yac::chat
