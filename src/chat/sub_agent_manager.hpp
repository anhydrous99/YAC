#pragma once

#include "chat/types.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <unordered_map>

namespace yac::chat {

namespace internal {

class ChatServiceToolApproval;

}  // namespace internal

inline constexpr int kMaxConcurrentSubAgents = 4;
inline constexpr int kDefaultSubAgentTimeoutSeconds = 300;
inline constexpr size_t kMaxResultChars = 4096;

class SubAgentManager {
 public:
  using EmitEventFn = std::function<void(ChatEvent)>;
  using NextMessageIdFn = std::function<ChatMessageId()>;
  using ConfigSnapshotFn = std::function<ChatConfig()>;

  SubAgentManager(provider::ProviderRegistry& registry,
                  std::shared_ptr<tool_call::ToolExecutor> tool_executor,
                  internal::ChatServiceToolApproval& tool_approval,
                  EmitEventFn parent_emit,
                  ConfigSnapshotFn parent_config_snapshot,
                  NextMessageIdFn parent_next_message_id,
                  int timeout_seconds = kDefaultSubAgentTimeoutSeconds);
  ~SubAgentManager();

  SubAgentManager(const SubAgentManager&) = delete;
  SubAgentManager& operator=(const SubAgentManager&) = delete;
  SubAgentManager(SubAgentManager&&) = delete;
  SubAgentManager& operator=(SubAgentManager&&) = delete;

  [[nodiscard]] std::string SpawnForeground(const std::string& task);
  [[nodiscard]] std::string SpawnBackground(const std::string& task);
  void Cancel(const std::string& agent_id);
  void CancelAll();
  [[nodiscard]] bool IsAtCapacity();
  [[nodiscard]] std::mutex* GetApprovalGate();

 private:
  struct SubAgentSession;

  void CleanupFinishedSessions();

  provider::ProviderRegistry* registry_;
  std::shared_ptr<tool_call::ToolExecutor> tool_executor_;
  internal::ChatServiceToolApproval* tool_approval_;
  EmitEventFn parent_emit_;
  ConfigSnapshotFn parent_config_snapshot_;
  NextMessageIdFn parent_next_message_id_;
  int timeout_seconds_;

  std::mutex approval_gate_;
  mutable std::shared_mutex sessions_mutex_;
  std::unordered_map<std::string, std::unique_ptr<SubAgentSession>>
      active_sessions_;
};

}  // namespace yac::chat
