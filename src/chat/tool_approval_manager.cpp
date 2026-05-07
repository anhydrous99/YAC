#include "chat/tool_approval_manager.hpp"

#include <algorithm>
#include <utility>

namespace yac::chat {

ToolApprovalManager::ToolApprovalManager(EmitEventFn emit_event)
    : emit_event_(std::move(emit_event)) {}

ApprovalId ToolApprovalManager::RequestApproval(ToolCallId tool_call_id,
                                                std::string tool_name,
                                                std::string tool_args) {
  ApprovalId approval_id{"tool-" +
                         std::to_string(next_approval_id_.fetch_add(1))};
  PendingApproval entry{
      .id = approval_id,
      .tool_call_id = std::move(tool_call_id),
      .tool_name = std::move(tool_name),
      .tool_args = std::move(tool_args),
      .approved = std::nullopt,
      .response = std::nullopt,
  };
  std::scoped_lock lock(mutex_);
  pending_.emplace(approval_id, std::move(entry));
  return approval_id;
}

void ToolApprovalManager::ResolveToolApproval(const ApprovalId& approval_id,
                                              bool approved) {
  {
    std::scoped_lock lock(mutex_);
    auto it = pending_.find(approval_id);
    if (it == pending_.end()) {
      return;
    }
    it->second.approved = approved;
  }
  wake_.notify_all();
}

void ToolApprovalManager::ResolveAskUser(const ApprovalId& approval_id,
                                         std::string response) {
  {
    std::scoped_lock lock(mutex_);
    auto it = pending_.find(approval_id);
    if (it == pending_.end()) {
      return;
    }
    it->second.approved = true;
    it->second.response = std::move(response);
  }
  wake_.notify_all();
}

void ToolApprovalManager::CancelPending() {
  {
    std::scoped_lock lock(mutex_);
    if (pending_.empty()) {
      return;
    }
    for (auto& [id, entry] : pending_) {
      if (!entry.approved.has_value()) {
        entry.approved = false;
      }
    }
  }
  wake_.notify_all();
}

bool ToolApprovalManager::IsAwaitingApproval() const {
  std::scoped_lock lock(mutex_);
  return std::ranges::any_of(pending_, [](const auto& entry) {
    return !entry.second.approved.has_value();
  });
}

ApprovalResolution ToolApprovalManager::WaitForResolution(
    const ApprovalId& approval_id, std::stop_token stop_token) {
  // libc++#76807 workaround: see chat_service.cpp WorkerLoop.
  std::stop_callback wake_on_stop(stop_token, [this] { wake_.notify_all(); });
  std::unique_lock lock(mutex_);
  wake_.wait(lock, [&] {
    auto it = pending_.find(approval_id);
    return it == pending_.end() || it->second.approved.has_value() ||
           stop_token.stop_requested();
  });

  auto it = pending_.find(approval_id);
  if (it == pending_.end() || !it->second.approved.has_value() ||
      stop_token.stop_requested()) {
    if (it != pending_.end()) {
      pending_.erase(it);
    }
    return ApprovalResolution{};
  }

  ApprovalResolution resolution{
      .approved = *it->second.approved,
      .response = it->second.response.value_or(std::string{}),
  };
  pending_.erase(it);
  return resolution;
}

}  // namespace yac::chat
