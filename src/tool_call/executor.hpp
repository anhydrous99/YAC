#pragma once

#include "chat/types.hpp"
#include "core_types/tool_call_types.hpp"
#include "core_types/typed_ids.hpp"
#include "tool_call/lsp_client.hpp"
#include "tool_call/web_fetch.hpp"
#include "tool_call/web_search.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace yac::chat {
class SubAgentManager;
class ToolApprovalManager;
}  // namespace yac::chat

namespace yac::tool_call {

class TodoState;

struct PreparedToolCall {
  chat::ToolCallRequest request;
  ToolCallBlock preview;
  bool requires_approval = false;
  std::string approval_prompt;
  ::yac::ApprovalId approval_id;
  chat::ChatMessageId card_message_id = 0;
};

using ToolExecutionResult = yac::core_types::ToolExecutionResult;

// Bundle of per-call dependencies threaded through every dispatch function.
// Lets dispatch helpers take two args instead of seven (most of which they
// ignore) and keeps the dependency surface explicit at the call site.
struct ExecutionContext {
  const WorkspaceFilesystem& workspace_filesystem;
  const std::shared_ptr<ILspClient>& lsp_client;
  TodoState& todo_state;
  chat::SubAgentManager* sub_agent_manager;
  chat::ToolApprovalManager* tool_approval;
  WebFetchTransport* web_fetch_transport;
  WebFetchNetworkPolicy web_fetch_network_policy;
  WebSearchTransport* web_search_transport;
  const std::optional<WebSearchProviderConfig>* web_search_config;
  std::stop_token stop;
};

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
  void SetToolApproval(chat::ToolApprovalManager* tool_approval);
  void SetWebFetchTransport(WebFetchTransport* transport);
  void SetWebSearchConfig(const chat::WebSearchConfig& config);
  void SetWebSearchTransport(WebSearchTransport* transport);

 private:
  WorkspaceFilesystem workspace_filesystem_;
  std::shared_ptr<ILspClient> lsp_client_;
  TodoState& todo_state_;
  chat::SubAgentManager* sub_agent_manager_ = nullptr;
  chat::ToolApprovalManager* tool_approval_ = nullptr;
  CurlWebFetchTransport default_web_fetch_transport_;
  WebFetchTransport* web_fetch_transport_ = &default_web_fetch_transport_;
  WebFetchNetworkPolicy web_fetch_network_policy_ =
      WebFetchNetworkPolicy::RealNetwork;
  CurlWebSearchTransport default_web_search_transport_;
  WebSearchTransport* web_search_transport_ = &default_web_search_transport_;
  std::optional<WebSearchProviderConfig> web_search_config_;
};

}  // namespace yac::tool_call
