#pragma once

#include "core_types/typed_ids.hpp"
#include "presentation/chat_ui_actions.hpp"

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace yac::test {

struct MockChatActions : public yac::presentation::IChatActions {
  std::vector<std::string> sent_messages;
  std::vector<std::string> commands;
  std::vector<std::pair<::yac::ApprovalId, bool>> tool_approvals;
  std::vector<std::pair<::yac::ApprovalId, std::string>> ask_user_responses;
  std::vector<::yac::ApprovalId> ask_user_cancels;
  int mode_toggles = 0;

  std::function<void(const std::string&)> on_send;
  std::function<void(const std::string&)> on_command;
  std::function<void(const ::yac::ApprovalId&, bool)> on_tool_approval;
  std::function<void(::yac::ApprovalId, std::string)> on_ask_user_response;
  std::function<void(::yac::ApprovalId)> on_ask_user_cancel;
  std::function<void()> on_mode_toggle;

  void OnSend(const std::string& message) override {
    sent_messages.push_back(message);
    if (on_send) {
      on_send(message);
    }
  }

  void OnCommand(const std::string& command) override {
    commands.push_back(command);
    if (on_command) {
      on_command(command);
    }
  }

  void OnToolApproval(const ::yac::ApprovalId& approval_id,
                      bool approved) override {
    tool_approvals.emplace_back(approval_id, approved);
    if (on_tool_approval) {
      on_tool_approval(approval_id, approved);
    }
  }

  void OnAskUserResponse(::yac::ApprovalId approval_id,
                         std::string response) override {
    auto id_copy = approval_id;
    auto response_copy = response;
    ask_user_responses.emplace_back(std::move(approval_id),
                                    std::move(response));
    if (on_ask_user_response) {
      on_ask_user_response(std::move(id_copy), std::move(response_copy));
    }
  }

  void OnAskUserCancel(::yac::ApprovalId approval_id) override {
    auto id_copy = approval_id;
    ask_user_cancels.push_back(std::move(approval_id));
    if (on_ask_user_cancel) {
      on_ask_user_cancel(std::move(id_copy));
    }
  }

  void OnModeToggle() override {
    ++mode_toggles;
    if (on_mode_toggle) {
      on_mode_toggle();
    }
  }
};

}  // namespace yac::test
