#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>

namespace yac::chat::internal {

struct ApprovalResolution {
  bool approved = false;
  std::string response;
};

class ChatServiceToolApproval {
 public:
  [[nodiscard]] std::string BeginPendingApproval();
  void Resolve(const std::string& approval_id, bool approved);
  void ResolveWithResponse(const std::string& approval_id, bool approved,
                           std::string response);
  void CancelPending();
  [[nodiscard]] ApprovalResolution WaitForResolution(
      const std::string& approval_id, std::stop_token stop_token);

 private:
  struct PendingApproval {
    std::string id;
    std::optional<bool> approved;
    std::optional<std::string> response;
  };

  std::mutex mutex_;
  std::condition_variable_any wake_;
  std::optional<PendingApproval> pending_;
  std::atomic<uint64_t> next_approval_id_{1};
};

}  // namespace yac::chat::internal
