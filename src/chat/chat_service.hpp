#pragma once

#include "chat/types.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace yac::chat {

using ChatEventCallback = std::function<void(ChatEvent)>;

namespace internal {

class ChatServicePromptProcessor;
class ChatServiceToolApproval;

}  // namespace internal

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
  void SetModel(std::string model);
  void CancelActiveResponse();
  void ResolveToolApproval(std::string approval_id, bool approved);
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
  void EmitEvent(ChatEvent event) const;
  void EmitQueueDepth();
  [[nodiscard]] ChatMessageId NextMessageId();
  [[nodiscard]] ChatConfig ConfigSnapshot() const;

  provider::ProviderRegistry registry_;
  ChatConfig config_;
  std::shared_ptr<::yac::tool_call::ToolExecutor> tool_executor_;
  std::unique_ptr<internal::ChatServiceToolApproval> tool_approval_;
  std::unique_ptr<internal::ChatServicePromptProcessor> prompt_processor_;

  mutable std::mutex mutex_;
  std::vector<ChatMessage> history_;
  std::deque<PendingPrompt> pending_;
  ChatEventCallback callback_;

  std::atomic<uint64_t> generation_{0};
  std::atomic<ChatMessageId> next_id_{1};

  std::jthread worker_;
  std::condition_variable_any wake_;
  std::optional<std::stop_source> active_stop_source_;
  bool active_ = false;
};

}  // namespace yac::chat
