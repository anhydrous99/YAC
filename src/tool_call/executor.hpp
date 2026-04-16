#pragma once

#include "chat/types.hpp"
#include "tool_call/lsp_client.hpp"
#include "tool_call/types.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>

namespace yac::tool_call {

struct PreparedToolCall {
  chat::ToolCallRequest request;
  ToolCallBlock preview;
  bool requires_approval = false;
  std::string approval_prompt;
};

struct ToolExecutionResult {
  ToolCallBlock block;
  std::string result_json;
  bool is_error = false;
};

class ToolExecutor {
 public:
  explicit ToolExecutor(std::filesystem::path workspace_root,
                        std::shared_ptr<ILspClient> lsp_client);

  [[nodiscard]] static std::vector<chat::ToolDefinition> Definitions();
  [[nodiscard]] static PreparedToolCall Prepare(
      const chat::ToolCallRequest& request);
  [[nodiscard]] ToolExecutionResult Execute(const PreparedToolCall& prepared,
                                            std::stop_token stop_token) const;

 private:
  [[nodiscard]] ToolExecutionResult ExecuteFileWrite(
      const chat::ToolCallRequest& request) const;
  [[nodiscard]] ToolExecutionResult ExecuteListDir(
      const chat::ToolCallRequest& request) const;
  [[nodiscard]] ToolExecutionResult ExecuteLspDiagnostics(
      const chat::ToolCallRequest& request) const;
  [[nodiscard]] ToolExecutionResult ExecuteLspReferences(
      const chat::ToolCallRequest& request) const;
  [[nodiscard]] ToolExecutionResult ExecuteLspGotoDefinition(
      const chat::ToolCallRequest& request) const;
  [[nodiscard]] ToolExecutionResult ExecuteLspRename(
      const chat::ToolCallRequest& request) const;
  [[nodiscard]] ToolExecutionResult ExecuteLspSymbols(
      const chat::ToolCallRequest& request) const;

  [[nodiscard]] std::filesystem::path ResolveWorkspacePath(
      const std::string& path) const;
  [[nodiscard]] std::string DisplayPath(
      const std::filesystem::path& path) const;

  std::filesystem::path workspace_root_;
  std::shared_ptr<ILspClient> lsp_client_;
};

}  // namespace yac::tool_call
