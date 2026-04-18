#pragma once

#include <cstdint>
#include <string>

namespace yac::chat {

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

struct TokenUsage {
  int prompt_tokens = 0;
  int completion_tokens = 0;
  int total_tokens = 0;
};

}  // namespace yac::chat
