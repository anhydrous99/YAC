#include "chat/chat_service_request_builder.hpp"

#include "tool_call/lsp_client.hpp"

#include <filesystem>
#include <utility>

namespace yac::chat::internal {

namespace {

std::vector<ChatMessage> WithSystemPrompt(std::vector<ChatMessage> messages,
                                          const ChatConfig& config) {
  if (config.system_prompt.has_value()) {
    messages.insert(messages.begin(),
                    ChatMessage{.role = ChatRole::System,
                                .status = ChatMessageStatus::Complete,
                                .content = *config.system_prompt});
  }
  return messages;
}

}  // namespace

ChatServiceRequestBuilder::ChatServiceRequestBuilder(ChatConfig config)
    : config_(std::move(config)) {}

const ChatConfig& ChatServiceRequestBuilder::Config() const {
  return config_;
}

ChatRequest ChatServiceRequestBuilder::BuildRequest(
    const std::vector<ChatMessage>& history,
    const std::vector<ToolDefinition>& tools) const {
  ChatRequest request;
  request.provider_id = config_.provider_id;
  request.model = config_.model;
  request.messages = WithSystemPrompt(history, config_);
  request.tools = tools;
  request.temperature = config_.temperature;
  request.stream = true;
  return request;
}

std::shared_ptr<::yac::tool_call::ToolExecutor> MakeChatToolExecutor(
    const ChatConfig& config) {
  auto root = config.workspace_root.empty()
                  ? std::filesystem::current_path()
                  : std::filesystem::path(config.workspace_root);
  auto lsp = std::make_shared<::yac::tool_call::JsonRpcLspClient>(
      ::yac::tool_call::LspServerConfig{
          .command = config.lsp_clangd_command,
          .args = config.lsp_clangd_args,
          .workspace_root = root,
      });
  return std::make_shared<::yac::tool_call::ToolExecutor>(std::move(root),
                                                          std::move(lsp));
}

}  // namespace yac::chat::internal
