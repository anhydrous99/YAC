#pragma once

#include "chat/types.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"

#include <memory>
#include <mutex>
#include <shared_mutex>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

namespace yac::core_types {
class IMcpManager;
}  // namespace yac::core_types

namespace yac::chat {

class ToolApprovalManager;

inline constexpr int kMaxConcurrentSubAgents = 4;
inline constexpr int kDefaultSubAgentTimeoutSeconds = 300;
inline constexpr size_t kMaxResultChars = 4096;

class SubAgentManager {
 public:
  using EmitEventFn = std::function<void(ChatEvent)>;
  using ConfigSnapshotFn = std::function<ChatConfig()>;
  using BackgroundResultFn =
      std::function<void(std::string tool_call_id, std::string task,
                         std::string result, bool is_error)>;

  SubAgentManager(provider::ProviderRegistry& registry,
                  std::shared_ptr<tool_call::ToolExecutor> tool_executor,
                  ToolApprovalManager& tool_approval, EmitEventFn parent_emit,
                  ConfigSnapshotFn parent_config_snapshot,
                  int timeout_seconds = kDefaultSubAgentTimeoutSeconds);
  ~SubAgentManager();

  SubAgentManager(const SubAgentManager&) = delete;
  SubAgentManager& operator=(const SubAgentManager&) = delete;
  SubAgentManager(SubAgentManager&&) = delete;
  SubAgentManager& operator=(SubAgentManager&&) = delete;

  void SetBackgroundResultCallback(BackgroundResultFn callback);
  void SetMcpManager(core_types::IMcpManager* mcp_manager);

  [[nodiscard]] std::string SpawnForeground(
      const std::string& task, ChatMessageId card_message_id,
      std::string tool_call_id, std::stop_token parent_stop_token = {});
  [[nodiscard]] std::string SpawnBackground(const std::string& task,
                                            ChatMessageId card_message_id,
                                            std::string tool_call_id);
  void Cancel(const std::string& agent_id);
  void CancelAll();
  [[nodiscard]] bool IsAtCapacity();
  [[nodiscard]] std::mutex* GetApprovalGate();

 private:
  struct SubAgentSession;
  struct SubAgentCompletion;

  [[nodiscard]] std::shared_ptr<SubAgentSession> CreateSession(
      const std::string& task, tool_call::SubAgentMode mode,
      ChatMessageId card_message_id, std::string tool_call_id);
  [[nodiscard]] bool TryStoreSession(
      const std::shared_ptr<SubAgentSession>& session);
  void RemoveSession(const std::string& agent_id);
  void MoveFinishedSessionsLocked(
      std::vector<std::shared_ptr<SubAgentSession>>& finished_sessions);
  void CleanupFinishedSessions();
  void AttachPromptProcessor(SubAgentSession& session);
  [[nodiscard]] EmitEventFn MakeFilteredEmit(SubAgentSession& session);
  [[nodiscard]] SubAgentCompletion RunSession(
      SubAgentSession& session, std::stop_token parent_stop_token = {});
  void EmitSessionCompleted(const SubAgentSession& session,
                            const SubAgentCompletion& completion);
  void DeliverBackgroundResult(const SubAgentSession& session,
                               const SubAgentCompletion& completion);
  static void RequestSessionStop(SubAgentSession& session, bool mark_cancelled);

  provider::ProviderRegistry* registry_;
  std::shared_ptr<tool_call::ToolExecutor> tool_executor_;
  ToolApprovalManager* tool_approval_;
  EmitEventFn parent_emit_;
  ConfigSnapshotFn parent_config_snapshot_;
  core_types::IMcpManager* mcp_manager_ = nullptr;
  int timeout_seconds_;

  std::mutex approval_gate_;
  mutable std::shared_mutex sessions_mutex_;
  std::unordered_map<std::string, std::shared_ptr<SubAgentSession>>
      active_sessions_;

  mutable std::mutex background_result_mutex_;
  BackgroundResultFn background_result_;
};

}  // namespace yac::chat
