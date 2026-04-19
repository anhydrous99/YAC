#pragma once

#include "core_types/chat_ids.hpp"
#include "core_types/tool_call_types.hpp"
#include "model_info.hpp"

#include <chrono>
#include <map>
#include <optional>
#include <string>
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
  ToolApprovalRequested,
  UsageReported,
  SubAgentProgress,
  SubAgentCompleted,
  SubAgentError,
  SubAgentCancelled,
};

struct ChatEvent {
  ChatEventType type = ChatEventType::Started;
  ChatMessageId message_id = 0;
  ChatRole role = ChatRole::Assistant;
  std::string text;
  std::string provider_id;
  std::string model;
  std::string tool_call_id;
  std::string tool_name;
  std::string approval_id;
  std::vector<ToolCallRequest> tool_calls;
  std::optional<::yac::tool_call::ToolCallBlock> tool_call;
  std::optional<TokenUsage> usage;
  ChatMessageStatus status = ChatMessageStatus::Complete;
  int queue_depth = 0;
  std::chrono::system_clock::time_point created_at =
      std::chrono::system_clock::now();
  std::string sub_agent_id = "";
  std::string sub_agent_task = "";
  std::string sub_agent_result = "";
  int sub_agent_tool_count = 0;
  int sub_agent_elapsed_ms = 0;
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
