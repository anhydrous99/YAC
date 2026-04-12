#pragma once

#include <chrono>
#include <map>
#include <string>
#include <vector>

namespace yac::chat {

enum class ChatRole { System, User, Assistant, Tool };

struct ChatMessage {
  ChatRole role = ChatRole::User;
  std::string content;
};

struct ChatRequest {
  std::string provider_id = "openai";
  std::string model = "gpt-4o-mini";
  std::vector<ChatMessage> messages;
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
};

struct ChatEvent {
  ChatEventType type = ChatEventType::Started;
  std::string text;
  std::string provider_id;
  std::string model;
  std::chrono::system_clock::time_point created_at =
      std::chrono::system_clock::now();
};

struct ProviderConfig {
  std::string id = "openai";
  std::string model = "gpt-4o-mini";
  std::string api_key;
  std::string api_key_env = "OPENAI_API_KEY";
  std::string base_url = "https://api.openai.com/v1/";
  std::map<std::string, std::string> options;
};

}  // namespace yac::chat
