#include "chat/chat_service_request_builder.hpp"

#include "tool_call/lsp_client.hpp"
#include "tool_call/todo_state.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <utility>

namespace yac::chat::internal {

namespace {

inline constexpr size_t kMaxAgentsMdSize = 8192;

[[nodiscard]] std::optional<std::string> ReadPromptFile(
    const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return std::nullopt;
  }

  std::string content(kMaxAgentsMdSize, '\0');
  file.read(content.data(), static_cast<std::streamsize>(content.size()));
  content.resize(
      std::min(kMaxAgentsMdSize, static_cast<size_t>(file.gcount())));
  return content;
}

[[nodiscard]] std::string LoadWorkspacePrompt(const ChatConfig& config) {
  const auto workspace_root =
      config.workspace_root.empty()
          ? std::filesystem::current_path()
          : std::filesystem::path(config.workspace_root);

  const auto agents_path = workspace_root / "AGENTS.md";
  if (auto content = ReadPromptFile(agents_path); content.has_value()) {
    return *content;
  }
  if (auto content = ReadPromptFile(workspace_root / "CLAUDE.md");
      content.has_value()) {
    return *content;
  }
  return {};
}

std::vector<ChatMessage> WithSystemPrompt(std::vector<ChatMessage> messages,
                                          const ChatConfig& config) {
  const auto workspace_prompt = LoadWorkspacePrompt(config);
  std::string final_prompt = workspace_prompt;
  if (config.system_prompt.has_value()) {
    if (!final_prompt.empty()) {
      final_prompt += "\n\n";
    }
    final_prompt += *config.system_prompt;
  }

  if (!final_prompt.empty()) {
    messages.insert(messages.begin(),
                    ChatMessage{.role = ChatRole::System,
                                .status = ChatMessageStatus::Complete,
                                .content = std::move(final_prompt)});
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
    const ChatConfig& config, ::yac::tool_call::TodoState& todo_state) {
  auto root = config.workspace_root.empty()
                  ? std::filesystem::current_path()
                  : std::filesystem::path(config.workspace_root);
  auto lsp = std::make_shared<::yac::tool_call::JsonRpcLspClient>(
      ::yac::tool_call::LspServerConfig{
          .command = config.lsp_clangd_command,
          .args = config.lsp_clangd_args,
          .workspace_root = root,
      });
  return std::make_shared<::yac::tool_call::ToolExecutor>(
      std::move(root), std::move(lsp), todo_state);
}

}  // namespace yac::chat::internal
