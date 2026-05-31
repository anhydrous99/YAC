#include "chat/chat_service_request_builder.hpp"

#include "provider/reasoning_effort_capability.hpp"
#include "tool_call/lsp_client.hpp"
#include "tool_call/todo_state.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
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

std::string PlanModeReminder(const std::string& active_plan_path) {
  return "<system-reminder>\n"
         "You are in Plan mode. The active plan file is `" +
         active_plan_path +
         "`.\n"
         "Work through the plan-file workflow: investigate the request, read "
         "and search the workspace, delegate research when useful, ask "
         "clarifying questions if the plan is blocked, and prepare the final "
         "implementation plan.\n"
         "Plan mode is read-only for workspace files and shell execution. Do "
         "not modify files until the plan is approved.\n"
         "When the plan is ready for approval, call `plan_exit` with the final "
         "plan.\n"
         "</system-reminder>";
}

std::string BuildSwitchReminder(const std::string& active_plan_path) {
  return "<system-reminder>\n"
         "The user approved the plan and switched from Plan mode to Build "
         "mode. The approved plan file is `" +
         active_plan_path +
         "`.\n"
         "Use that plan as the implementation guide, continue in Build mode, "
         "and make the requested code changes now.\n"
         "</system-reminder>";
}

void AppendSyntheticReminder(std::string& final_prompt, std::string reminder) {
  if (reminder.empty()) {
    return;
  }
  if (!final_prompt.empty()) {
    final_prompt += "\n\n";
  }
  final_prompt += std::move(reminder);
}

ProviderConfig ActiveProviderConfig(const ChatConfig& config) {
  return ProviderConfig{.id = config.provider_id,
                        .profile_id = config.profile_id,
                        .model = config.model,
                        .api_key = config.api_key,
                        .api_key_env = config.api_key_env,
                        .base_url = config.base_url,
                        .system_prompt = config.system_prompt,
                        .options = config.options,
                        .context_window = config.context_window,
                        .state_store = config.state_store};
}

std::optional<ReasoningEffort> ActiveReasoningEffort(const ChatConfig& config) {
  const auto settings_it =
      std::ranges::find_if(config.model_settings, [&](const auto& settings) {
        return settings.provider_id == config.provider_id &&
               settings.model == config.model;
      });
  if (settings_it == config.model_settings.end() ||
      !settings_it->effort.has_value()) {
    return std::nullopt;
  }
  const auto capability = ::yac::provider::LookupReasoningEffortCapability(
      ActiveProviderConfig(config), config.model.value);
  if (!capability.has_value()) {
    return std::nullopt;
  }
  const auto allowed =
      std::ranges::find(capability->allowed_values, *settings_it->effort);
  if (allowed == capability->allowed_values.end()) {
    return std::nullopt;
  }
  return settings_it->effort;
}

std::string ComposeSystemPrompt(const ChatConfig& config,
                                bool include_build_switch_reminder) {
  const auto workspace_prompt = LoadWorkspacePrompt(config);
  std::string final_prompt = workspace_prompt;
  if (config.system_prompt.has_value()) {
    if (!final_prompt.empty()) {
      final_prompt += "\n\n";
    }
    final_prompt += *config.system_prompt;
  }

  if (config.agent_mode == AgentMode::Plan && config.active_plan_path) {
    AppendSyntheticReminder(final_prompt,
                            PlanModeReminder(*config.active_plan_path));
  } else if (config.agent_mode == AgentMode::Build &&
             include_build_switch_reminder && config.build_switch_plan_path) {
    AppendSyntheticReminder(
        final_prompt, BuildSwitchReminder(*config.build_switch_plan_path));
  }

  return final_prompt;
}

std::vector<ChatMessage> WithSystemPrompt(std::vector<ChatMessage> messages,
                                          std::string final_prompt) {
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
  const bool include_build_switch_reminder =
      config_.build_switch_plan_path.has_value() &&
      !build_switch_reminder_used_;
  auto system_prompt =
      ComposeSystemPrompt(config_, include_build_switch_reminder);
  if (include_build_switch_reminder) {
    build_switch_reminder_used_ = true;
  }

  ChatRequest request;
  request.provider_id = config_.provider_id;
  request.model = config_.model;
  request.session_id = config_.chat_session_id;
  if (config_.provider_id.value == "openai" && !system_prompt.empty()) {
    request.responses_instructions = system_prompt;
  }
  request.messages = WithSystemPrompt(history, std::move(system_prompt));
  request.tools = tools;
  request.temperature = config_.temperature;
  request.reasoning_effort = ActiveReasoningEffort(config_);
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
