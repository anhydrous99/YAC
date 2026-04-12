#pragma once

#include "chat/types.hpp"
#include "provider/provider_registry.hpp"

#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yac::chat {

using ChatEventCallback = std::function<void(ChatEvent)>;

class ChatService {
 public:
  explicit ChatService(provider::ProviderRegistry registry,
                       std::string provider_id = "openai",
                       std::string model = "gpt-4o-mini");
  ~ChatService();

  ChatService(const ChatService&) = delete;
  ChatService& operator=(const ChatService&) = delete;
  ChatService(ChatService&&) = delete;
  ChatService& operator=(ChatService&&) = delete;

  void SubmitUserMessage(std::string content, ChatEventCallback callback);
  void CancelAll();

  [[nodiscard]] std::vector<ChatMessage> History() const;

 private:
  void RecordAssistantMessage(const std::string& content);

  provider::ProviderRegistry registry_;
  std::string provider_id_;
  std::string model_;
  mutable std::mutex mutex_;
  std::vector<ChatMessage> history_;
  std::vector<std::jthread> requests_;
};

}  // namespace yac::chat
