#pragma once

#include "core_types/typed_ids.hpp"

#include <string>

namespace yac::presentation {

struct IChatActions {
  virtual ~IChatActions() = default;
  virtual void OnSend(const std::string& message) = 0;
  virtual void OnCommand(const std::string& command) = 0;
  virtual void OnToolApproval(const ::yac::ApprovalId& approval_id,
                              bool approved) = 0;
  virtual void OnAskUserResponse(::yac::ApprovalId approval_id,
                                 std::string response) = 0;
  virtual void OnAskUserCancel(::yac::ApprovalId approval_id) = 0;
  virtual void OnModeToggle() = 0;
};

struct NoOpChatActions : public IChatActions {
  void OnSend(const std::string&) override {}
  void OnCommand(const std::string&) override {}
  void OnToolApproval(const ::yac::ApprovalId&, bool) override {}
  void OnAskUserResponse(::yac::ApprovalId, std::string) override {}
  void OnAskUserCancel(::yac::ApprovalId) override {}
  void OnModeToggle() override {}
};

}  // namespace yac::presentation
