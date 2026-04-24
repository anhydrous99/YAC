#pragma once

#include "chat/sub_agent_manager.hpp"
#include "chat/types.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/todo_state.hpp"

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
  void ResolveAskUser(const std::string& approval_id, std::string response);
  [[nodiscard]] AgentMode GetAgentMode() const;
  void SetAgentMode(AgentMode mode);
  void ResetConversation();
  void CompactConversation(decltype(sizeof(0)) keep_last = 10);
  SubAgentManager& GetSubAgentManager() { return *sub_agent_manager_; }
  // Spawns a background sub-agent initiated by the user (e.g., /task).
  // Creates the UI card via a synthetic ToolCallStarted event and returns
  // the agent id. On completion the result is delivered via
  // HandleBackgroundSubAgentResult (same path as model-initiated spawns).
  std::string SpawnBackgroundSubAgent(std::string task);

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
  void HandleBackgroundSubAgentResult(std::string tool_call_id,
                                      std::string task, std::string result,
                                      bool is_error);
  void InjectSubAgentContinuation(std::string body);

  static constexpr const char* kSubAgentRoleLabel = "Sub-agent";

  provider::ProviderRegistry registry_;
  ChatConfig config_;
  ::yac::tool_call::TodoState todo_state_;
  std::shared_ptr<::yac::tool_call::ToolExecutor> tool_executor_;
  std::unique_ptr<internal::ChatServiceToolApproval> tool_approval_;
  std::unique_ptr<SubAgentManager> sub_agent_manager_;
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
