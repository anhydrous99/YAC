#pragma once

#include "chat/types.hpp"
#include "core_types/typed_ids.hpp"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_map>

namespace yac::chat {

struct ApprovalResolution {
  bool approved = false;
  std::string response;
};

// Owns pending tool approvals (extracted from ChatService in T16).
// Thread-safe via internal mutex + condition variable; WaitForResolution
// uses a stop_callback to wake on cancellation. The optional EmitEventFn
// is a forward hook — current event emission still lives in
// ChatServicePromptProcessor, per the T16 scope constraint.
class ToolApprovalManager {
 public:
  using EmitEventFn = std::function<void(ChatEvent)>;

  ToolApprovalManager() = default;
  explicit ToolApprovalManager(EmitEventFn emit_event);
  ~ToolApprovalManager() = default;

  ToolApprovalManager(const ToolApprovalManager&) = delete;
  ToolApprovalManager& operator=(const ToolApprovalManager&) = delete;
  ToolApprovalManager(ToolApprovalManager&&) = delete;
  ToolApprovalManager& operator=(ToolApprovalManager&&) = delete;

  [[nodiscard]] ApprovalId RequestApproval(ToolCallId tool_call_id = {},
                                           std::string tool_name = {},
                                           std::string tool_args = {});
  void ResolveToolApproval(const ApprovalId& approval_id, bool approved);
  void ResolveAskUser(const ApprovalId& approval_id, std::string response);
  void CancelPending();
  [[nodiscard]] bool IsAwaitingApproval() const;
  [[nodiscard]] ApprovalResolution WaitForResolution(
      const ApprovalId& approval_id, std::stop_token stop_token);

 private:
  struct PendingApproval {
    ApprovalId id;
    ToolCallId tool_call_id;
    std::string tool_name;
    std::string tool_args;
    std::optional<bool> approved;
    std::optional<std::string> response;
  };

  EmitEventFn emit_event_;
  mutable std::mutex mutex_;
  std::condition_variable_any wake_;
  std::unordered_map<ApprovalId, PendingApproval> pending_;
  std::atomic<uint64_t> next_approval_id_{1};
};

}  // namespace yac::chat
