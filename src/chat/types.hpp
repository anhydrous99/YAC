#pragma once

#include "model_info.hpp"
#include "tool_call/types.hpp"

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace yac::chat {

// Stable identifier for messages across service and presentation layers.
using ChatMessageId = uint64_t;

enum class ChatRole { System, User, Assistant, Tool };

enum class ChatMessageStatus {
  Queued,
  Active,
  Complete,
  Cancelled,
  Error,
};

struct ToolDefinition {
  std::string name;
  std::string description;
  std::string parameters_schema_json;
};

struct ToolCallRequest {
  std::string id;
  std::string name;
  std::string arguments_json;
};

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
  ChatMessageStatus status = ChatMessageStatus::Complete;
  int queue_depth = 0;
  std::chrono::system_clock::time_point created_at =
      std::chrono::system_clock::now();
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

}  // namespace yac::chat
