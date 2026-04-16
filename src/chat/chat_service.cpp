#include "chat/chat_service.hpp"

#include "tool_call/lsp_client.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <utility>

namespace yac::chat {

namespace {

constexpr int kMaxToolRounds = 8;

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

std::string ToolRejectedJson() {
  return R"({"error":"User rejected tool execution."})";
}

std::shared_ptr<::yac::tool_call::ToolExecutor> MakeToolExecutor(
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

}  // namespace

ChatService::ChatService(provider::ProviderRegistry registry, ChatConfig config)
    : registry_(std::move(registry)),
      config_(std::move(config)),
      tool_executor_(MakeToolExecutor(config_)) {
  worker_ = std::jthread([this](std::stop_token st) { WorkerLoop(st); });
}

ChatService::~ChatService() {
  {
    std::lock_guard lock(mutex_);
    if (active_stop_source_.has_value()) {
      active_stop_source_->request_stop();
    }
  }
  worker_.request_stop();
  wake_.notify_one();
}

void ChatService::SetEventCallback(ChatEventCallback callback) {
  std::lock_guard lock(mutex_);
  callback_ = std::move(callback);
}

ChatMessageId ChatService::SubmitUserMessage(std::string content) {
  auto id = NextMessageId();
  auto queued_content = content;
  {
    std::lock_guard lock(mutex_);
    pending_.push_back({id, std::move(content)});
  }
  wake_.notify_one();

  EmitEvent(ChatEvent{.type = ChatEventType::UserMessageQueued,
                      .message_id = id,
                      .role = ChatRole::User,
                      .text = std::move(queued_content),
                      .status = ChatMessageStatus::Queued});
  EmitQueueDepth();
  return id;
}

void ChatService::SetModel(std::string model) {
  std::string provider_id;
  std::string new_model;
  {
    std::lock_guard lock(mutex_);
    if (config_.model == model) {
      return;
    }
    config_.model = model;
    provider_id = config_.provider_id;
    new_model = config_.model;
  }

  EmitEvent(ChatEvent{.type = ChatEventType::ModelChanged,
                      .provider_id = std::move(provider_id),
                      .model = std::move(new_model)});
}

void ChatService::CancelActiveResponse() {
  std::lock_guard lock(mutex_);
  if (!active_) {
    return;
  }
  generation_.fetch_add(1);
  if (active_stop_source_.has_value()) {
    active_stop_source_->request_stop();
  }
  if (pending_approval_.has_value()) {
    pending_approval_->approved = false;
    approval_wake_.notify_all();
  }
}

void ChatService::ResolveToolApproval(std::string approval_id, bool approved) {
  {
    std::lock_guard lock(mutex_);
    if (!pending_approval_.has_value() ||
        pending_approval_->id != approval_id) {
      return;
    }
    pending_approval_->approved = approved;
  }
  approval_wake_.notify_all();
}

void ChatService::ResetConversation() {
  uint64_t old_gen = generation_.fetch_add(1);
  (void)old_gen;

  {
    std::lock_guard lock(mutex_);
    if (active_stop_source_.has_value()) {
      active_stop_source_->request_stop();
    }
    history_.clear();
    pending_.clear();
    active_ = false;
    active_stop_source_.reset();
    if (pending_approval_.has_value()) {
      pending_approval_->approved = false;
    }
  }
  wake_.notify_one();
  approval_wake_.notify_all();

  EmitEvent(ChatEvent{.type = ChatEventType::ConversationCleared});
}

std::vector<ChatMessage> ChatService::History() const {
  std::lock_guard lock(mutex_);
  return history_;
}

bool ChatService::IsBusy() const {
  std::lock_guard lock(mutex_);
  return active_ || !pending_.empty();
}

int ChatService::QueueDepth() const {
  std::lock_guard lock(mutex_);
  return static_cast<int>(pending_.size());
}

void ChatService::WorkerLoop(std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    PendingPrompt prompt;
    std::stop_source request_stop_source;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, stop_token, [&] {
        return !pending_.empty() || stop_token.stop_requested();
      });
      if (stop_token.stop_requested()) {
        return;
      }
      if (pending_.empty()) {
        continue;
      }
      prompt = std::move(pending_.front());
      pending_.pop_front();
      active_ = true;
      request_stop_source = std::stop_source{};
      active_stop_source_ = request_stop_source;
    }

    EmitEvent(ChatEvent{.type = ChatEventType::UserMessageActive,
                        .message_id = prompt.id,
                        .role = ChatRole::User,
                        .status = ChatMessageStatus::Active});
    EmitQueueDepth();

    ProcessPrompt(prompt, generation_.load(), request_stop_source.get_token());

    {
      std::lock_guard lock(mutex_);
      active_ = false;
      active_stop_source_.reset();
    }
  }
}

void ChatService::ProcessPrompt(const PendingPrompt& prompt,
                                uint64_t generation,
                                std::stop_token stop_token) {
  const auto assistant_id = NextMessageId();
  const auto config = ConfigSnapshot();
  auto provider = registry_.Resolve(config.provider_id);
  if (provider == nullptr) {
    EmitEvent(ChatEvent{.type = ChatEventType::MessageStatusChanged,
                        .message_id = prompt.id,
                        .role = ChatRole::User,
                        .status = ChatMessageStatus::Complete});
    EmitEvent(ChatEvent{
        .type = ChatEventType::Error,
        .message_id = assistant_id,
        .role = ChatRole::Assistant,
        .text = "No provider registered for '" + config.provider_id + "'.",
        .status = ChatMessageStatus::Error});
    EmitEvent(
        ChatEvent{.type = ChatEventType::Finished, .message_id = assistant_id});
    return;
  }

  {
    std::lock_guard lock(mutex_);
    history_.push_back(ChatMessage{.id = prompt.id,
                                   .role = ChatRole::User,
                                   .status = ChatMessageStatus::Active,
                                   .content = prompt.content});
  }

  EmitEvent(ChatEvent{.type = ChatEventType::MessageStatusChanged,
                      .message_id = prompt.id,
                      .role = ChatRole::User,
                      .status = ChatMessageStatus::Complete});
  EmitEvent(ChatEvent{.type = ChatEventType::Started,
                      .message_id = assistant_id,
                      .role = ChatRole::Assistant,
                      .provider_id = config.provider_id,
                      .model = config.model,
                      .status = ChatMessageStatus::Active});

  std::string visible_assistant_text;
  bool assistant_error = false;
  for (int round = 0; round <= kMaxToolRounds; ++round) {
    std::string round_text;
    std::vector<ToolCallRequest> requested_tools;
    ChatRequest request = BuildRequest(config);
    {
      std::lock_guard lock(mutex_);
      request.messages = WithSystemPrompt(history_, config);
    }
    request.tools = tool_executor_->Definitions();

    auto sink = [this, &round_text, assistant_id, generation, &assistant_error,
                 &requested_tools](ChatEvent event) mutable {
      if (generation_.load() != generation) {
        return;
      }
      if (event.type == ChatEventType::ToolCallRequested) {
        requested_tools = std::move(event.tool_calls);
        return;
      }
      event.message_id = assistant_id;
      event.role = ChatRole::Assistant;
      if (event.type == ChatEventType::TextDelta) {
        if (event.text.empty()) {
          return;
        }
        round_text += event.text;
      } else if (event.type == ChatEventType::Error) {
        assistant_error = true;
        event.status = ChatMessageStatus::Error;
      }
      EmitEvent(std::move(event));
    };

    provider->CompleteStream(request, std::move(sink), stop_token);

    if (generation_.load() != generation) {
      EmitEvent(ChatEvent{.type = ChatEventType::MessageStatusChanged,
                          .message_id = assistant_id,
                          .role = ChatRole::Assistant,
                          .status = ChatMessageStatus::Cancelled});
      EmitEvent(ChatEvent{.type = ChatEventType::Finished,
                          .message_id = assistant_id});
      return;
    }

    if (assistant_error) {
      EmitEvent(ChatEvent{.type = ChatEventType::Finished,
                          .message_id = assistant_id});
      return;
    }

    visible_assistant_text += round_text;

    if (requested_tools.empty()) {
      break;
    }

    {
      std::lock_guard lock(mutex_);
      history_.push_back(ChatMessage{.id = assistant_id,
                                     .role = ChatRole::Assistant,
                                     .status = ChatMessageStatus::Complete,
                                     .content = round_text,
                                     .tool_calls = requested_tools});
    }

    for (const auto& tool_request : requested_tools) {
      auto tool_message_id = NextMessageId();
      auto prepared = tool_executor_->Prepare(tool_request);
      EmitEvent(ChatEvent{.type = ChatEventType::ToolCallStarted,
                          .message_id = tool_message_id,
                          .role = ChatRole::Tool,
                          .tool_call_id = tool_request.id,
                          .tool_name = tool_request.name,
                          .tool_call = prepared.preview,
                          .status = ChatMessageStatus::Active});

      bool approved = true;
      if (prepared.requires_approval) {
        auto approval_id =
            "tool-" + std::to_string(next_approval_id_.fetch_add(1));
        {
          std::lock_guard lock(mutex_);
          pending_approval_ = PendingApproval{.id = approval_id};
        }
        EmitEvent(ChatEvent{.type = ChatEventType::ToolApprovalRequested,
                            .message_id = tool_message_id,
                            .role = ChatRole::Tool,
                            .text = prepared.approval_prompt,
                            .tool_call_id = tool_request.id,
                            .tool_name = tool_request.name,
                            .approval_id = approval_id,
                            .tool_call = prepared.preview,
                            .status = ChatMessageStatus::Queued});
        approved = WaitForApproval(approval_id, stop_token);
      }

      ::yac::tool_call::ToolExecutionResult result =
          approved ? tool_executor_->Execute(prepared, stop_token)
                   : ::yac::tool_call::ToolExecutionResult{
                         .block = prepared.preview,
                         .result_json = ToolRejectedJson(),
                         .is_error = true};
      if (!approved) {
        std::visit(
            [](auto& call) {
              if constexpr (requires {
                              call.is_error;
                              call.error;
                            }) {
                call.is_error = true;
                call.error = "User rejected tool execution.";
              }
            },
            result.block);
      }

      EmitEvent(ChatEvent{.type = ChatEventType::ToolCallDone,
                          .message_id = tool_message_id,
                          .role = ChatRole::Tool,
                          .tool_call_id = tool_request.id,
                          .tool_name = tool_request.name,
                          .tool_call = result.block,
                          .status = result.is_error
                                        ? ChatMessageStatus::Error
                                        : ChatMessageStatus::Complete});
      {
        std::lock_guard lock(mutex_);
        history_.push_back(
            ChatMessage{.id = tool_message_id,
                        .role = ChatRole::Tool,
                        .status = result.is_error ? ChatMessageStatus::Error
                                                  : ChatMessageStatus::Complete,
                        .content = result.result_json,
                        .tool_call_id = tool_request.id,
                        .tool_name = tool_request.name});
      }

      if (stop_token.stop_requested()) {
        break;
      }
    }

    if (round == kMaxToolRounds) {
      EmitEvent(ChatEvent{.type = ChatEventType::Error,
                          .message_id = assistant_id,
                          .role = ChatRole::Assistant,
                          .text = "Tool round limit reached.",
                          .status = ChatMessageStatus::Error});
      EmitEvent(ChatEvent{.type = ChatEventType::Finished,
                          .message_id = assistant_id});
      return;
    }
  }

  if (!visible_assistant_text.empty()) {
    std::lock_guard lock(mutex_);
    history_.push_back(ChatMessage{.id = assistant_id,
                                   .role = ChatRole::Assistant,
                                   .status = ChatMessageStatus::Complete,
                                   .content = visible_assistant_text});
  }
  EmitEvent(ChatEvent{.type = ChatEventType::AssistantMessageDone,
                      .message_id = assistant_id,
                      .role = ChatRole::Assistant,
                      .status = ChatMessageStatus::Complete});
  EmitEvent(
      ChatEvent{.type = ChatEventType::Finished, .message_id = assistant_id});
}

bool ChatService::WaitForApproval(const std::string& approval_id,
                                  std::stop_token stop_token) {
  std::unique_lock lock(mutex_);
  approval_wake_.wait(lock, stop_token, [&] {
    return !pending_approval_.has_value() ||
           pending_approval_->id != approval_id ||
           pending_approval_->approved.has_value();
  });
  if (stop_token.stop_requested() || !pending_approval_.has_value() ||
      pending_approval_->id != approval_id ||
      !pending_approval_->approved.has_value()) {
    pending_approval_.reset();
    return false;
  }
  const bool approved = *pending_approval_->approved;
  pending_approval_.reset();
  return approved;
}

void ChatService::EmitEvent(ChatEvent event) const {
  ChatEventCallback cb;
  {
    std::lock_guard lock(mutex_);
    cb = callback_;
  }
  if (cb) {
    cb(std::move(event));
  }
}

void ChatService::EmitQueueDepth() {
  int depth = 0;
  {
    std::lock_guard lock(mutex_);
    depth = static_cast<int>(pending_.size());
  }
  EmitEvent(ChatEvent{.type = ChatEventType::QueueDepthChanged,
                      .queue_depth = depth});
}

ChatMessageId ChatService::NextMessageId() {
  return next_id_.fetch_add(1);
}

ChatConfig ChatService::ConfigSnapshot() const {
  std::lock_guard lock(mutex_);
  return config_;
}

ChatRequest ChatService::BuildRequest(const ChatConfig& config) {
  ChatRequest request;
  request.provider_id = config.provider_id;
  request.model = config.model;
  request.temperature = config.temperature;
  request.stream = true;
  return request;
}

}  // namespace yac::chat
