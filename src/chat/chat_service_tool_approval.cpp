#include "chat/chat_service_tool_approval.hpp"

namespace yac::chat::internal {

std::string ChatServiceToolApproval::BeginPendingApproval() {
  auto approval_id = "tool-" + std::to_string(next_approval_id_.fetch_add(1));
  {
    std::lock_guard lock(mutex_);
    pending_ = PendingApproval{.id = approval_id};
  }
  return approval_id;
}

void ChatServiceToolApproval::Resolve(const std::string& approval_id,
                                      bool approved) {
  {
    std::lock_guard lock(mutex_);
    if (!pending_.has_value() || pending_->id != approval_id) {
      return;
    }
    pending_->approved = approved;
  }
  wake_.notify_all();
}

void ChatServiceToolApproval::CancelPending() {
  {
    std::lock_guard lock(mutex_);
    if (!pending_.has_value()) {
      return;
    }
    pending_->approved = false;
  }
  wake_.notify_all();
}

bool ChatServiceToolApproval::WaitForResolution(const std::string& approval_id,
                                                std::stop_token stop_token) {
  std::unique_lock lock(mutex_);
  wake_.wait(lock, stop_token, [&] {
    return !pending_.has_value() || pending_->id != approval_id ||
           pending_->approved.has_value();
  });
  if (stop_token.stop_requested() || !pending_.has_value() ||
      pending_->id != approval_id || !pending_->approved.has_value()) {
    pending_.reset();
    return false;
  }
  const bool approved = *pending_->approved;
  pending_.reset();
  return approved;
}

}  // namespace yac::chat::internal
