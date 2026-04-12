#pragma once

#include "chat/types.hpp"
#include "provider/provider_registry.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace yac::chat {

using ChatEventCallback = std::function<void(ChatEvent)>;

class ChatService {
 public:
  explicit ChatService(provider::ProviderRegistry registry,
                       ChatConfig config = {});
  ~ChatService();

  ChatService(const ChatService&) = delete;
  ChatService& operator=(const ChatService&) = delete;
  ChatService(ChatService&&) = delete;
  ChatService& operator=(ChatService&&) = delete;

  void SetEventCallback(ChatEventCallback callback);
  ChatMessageId SubmitUserMessage(std::string content);
  void CancelActiveResponse();
  void ResetConversation();

  [[nodiscard]] std::vector<ChatMessage> History() const;
  [[nodiscard]] bool IsBusy() const;
  [[nodiscard]] int QueueDepth() const;

 private:
  struct PendingPrompt {
    ChatMessageId id;
    std::string content;
  };

  void WorkerLoop(std::stop_token stop_token);
  void ProcessPrompt(const PendingPrompt& prompt, uint64_t generation);
  void EmitEvent(ChatEvent event) const;
  void EmitQueueDepth();
  [[nodiscard]] ChatMessageId NextMessageId();
  [[nodiscard]] ChatRequest BuildRequest() const;

  provider::ProviderRegistry registry_;
  ChatConfig config_;

  mutable std::mutex mutex_;
  std::vector<ChatMessage> history_;
  std::deque<PendingPrompt> pending_;
  ChatEventCallback callback_;

  std::atomic<uint64_t> generation_{0};
  std::atomic<ChatMessageId> next_id_{1};

  std::jthread worker_;
  std::condition_variable_any wake_;
  bool active_ = false;
};

}  // namespace yac::chat
