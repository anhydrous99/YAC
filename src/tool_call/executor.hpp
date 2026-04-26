#pragma once

#include "chat/types.hpp"
#include "core_types/tool_call_types.hpp"
#include "tool_call/lsp_client.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace yac::chat {
class SubAgentManager;
namespace internal {
class ChatServiceToolApproval;
}  // namespace internal
}  // namespace yac::chat

namespace yac::tool_call {

class TodoState;

struct PreparedToolCall {
  chat::ToolCallRequest request;
  ToolCallBlock preview;
  bool requires_approval = false;
  std::string approval_prompt;
  std::string approval_id;
  chat::ChatMessageId card_message_id = 0;
};

using ToolExecutionResult = yac::core_types::ToolExecutionResult;

class ToolExecutor {
 public:
  explicit ToolExecutor(std::filesystem::path workspace_root,
                        std::shared_ptr<ILspClient> lsp_client,
                        TodoState& todo_state);

  [[nodiscard]] static std::vector<chat::ToolDefinition> Definitions();
  [[nodiscard]] static PreparedToolCall Prepare(
      const chat::ToolCallRequest& request);
  [[nodiscard]] ToolExecutionResult Execute(const PreparedToolCall& prepared,
                                            std::stop_token stop_token) const;
  void SetSubAgentManager(chat::SubAgentManager* manager);
  void SetToolApproval(chat::internal::ChatServiceToolApproval* tool_approval);

 private:
  WorkspaceFilesystem workspace_filesystem_;
  std::shared_ptr<ILspClient> lsp_client_;
  TodoState& todo_state_;
  chat::SubAgentManager* sub_agent_manager_ = nullptr;
  chat::internal::ChatServiceToolApproval* tool_approval_ = nullptr;
};

}  // namespace yac::tool_call
