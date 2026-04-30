#include "chat/chat_service_tool_approval.hpp"

namespace yac::chat::internal {

std::string ChatServiceToolApproval::BeginPendingApproval() {
  auto approval_id = "tool-" + std::to_string(next_approval_id_.fetch_add(1));
  {
    std::scoped_lock lock(mutex_);
    pending_ = PendingApproval{.id = approval_id};
  }
  return approval_id;
}

void ChatServiceToolApproval::Resolve(const std::string& approval_id,
                                      bool approved) {
  {
    std::scoped_lock lock(mutex_);
    if (!pending_.has_value() || pending_->id != approval_id) {
      return;
    }
    pending_->approved = approved;
  }
  wake_.notify_all();
}

void ChatServiceToolApproval::ResolveWithResponse(
    const std::string& approval_id, bool approved, std::string response) {
  {
    std::scoped_lock lock(mutex_);
    if (!pending_.has_value() || pending_->id != approval_id) {
      return;
    }
    pending_->approved = approved;
    pending_->response = std::move(response);
  }
  wake_.notify_all();
}

void ChatServiceToolApproval::CancelPending() {
  {
    std::scoped_lock lock(mutex_);
    if (!pending_.has_value()) {
      return;
    }
    pending_->approved = false;
  }
  wake_.notify_all();
}

ApprovalResolution ChatServiceToolApproval::WaitForResolution(
    const std::string& approval_id, std::stop_token stop_token) {
  // libc++#76807 workaround: see chat_service.cpp WorkerLoop.
  std::stop_callback wake_on_stop(stop_token, [this] { wake_.notify_all(); });
  std::unique_lock lock(mutex_);
  wake_.wait(lock, [&] {
    return !pending_.has_value() || pending_->id != approval_id ||
           pending_->approved.has_value() || stop_token.stop_requested();
  });
  if (stop_token.stop_requested() || !pending_.has_value() ||
      pending_->id != approval_id || !pending_->approved.has_value()) {
    pending_.reset();
    return ApprovalResolution{};
  }
  ApprovalResolution resolution{
      .approved = *pending_->approved,
      .response = pending_->response.value_or(std::string{}),
  };
  pending_.reset();
  return resolution;
}

}  // namespace yac::chat::internal
