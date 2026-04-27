#include "chat/chat_service.hpp"

#include "chat/chat_service_history.hpp"
#include "chat/chat_service_prompt_processor.hpp"
#include "chat/chat_service_request_builder.hpp"
#include "chat/chat_service_tool_approval.hpp"

#include <memory>
#include <utility>

namespace yac::chat {

ChatService::ChatService(provider::ProviderRegistry registry, ChatConfig config,
                         std::unique_ptr<core_types::IMcpManager> mcp_manager)
    : registry_(std::move(registry)),
      config_(std::move(config)),
      mcp_manager_(std::move(mcp_manager)),
      mcp_helper_(mcp_manager_ ? std::make_unique<internal::ChatServiceMcp>(
                                     mcp_manager_.get())
                               : nullptr),
      tool_executor_(internal::MakeChatToolExecutor(config_, todo_state_)),
      tool_approval_(std::make_unique<internal::ChatServiceToolApproval>()),
      sub_agent_manager_(std::make_unique<SubAgentManager>(
          registry_, tool_executor_, *tool_approval_,
          [this](ChatEvent event) { EmitEvent(std::move(event)); },
          [this] { return ConfigSnapshot(); })),
      prompt_processor_(std::make_unique<internal::ChatServicePromptProcessor>(
          registry_, *tool_executor_, *tool_approval_, mcp_helper_.get(),
          mutex_, history_,
          [this](ChatEvent event) { EmitEvent(std::move(event)); },
          [this] { return NextMessageId(); },
          [this] { return ConfigSnapshot(); },
          [this] { return generation_.load(); }, std::set<std::string>{},
          sub_agent_manager_->GetApprovalGate(),
          [this] { return ExcludedToolsForMode(config_.agent_mode); })) {
  tool_executor_->SetSubAgentManager(sub_agent_manager_.get());
  tool_executor_->SetToolApproval(tool_approval_.get());
  sub_agent_manager_->SetMcpManager(mcp_manager_.get());
  sub_agent_manager_->SetBackgroundResultCallback(
      [this](std::string tool_call_id, std::string task, std::string result,
             bool is_error) {
        HandleBackgroundSubAgentResult(std::move(tool_call_id), std::move(task),
                                       std::move(result), is_error);
      });
  worker_ = std::jthread([this](std::stop_token st) { WorkerLoop(st); });
}

ChatService::~ChatService() {
  {
    std::lock_guard lock(mutex_);
    if (active_stop_source_.has_value()) {
      active_stop_source_->request_stop();
    }
  }
  tool_approval_->CancelPending();
  worker_.request_stop();
  wake_.notify_one();
  if (worker_.joinable()) {
    worker_.join();
  }
  sub_agent_manager_.reset();
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

  EmitEvent(
      ChatEvent{UserMessageQueuedEvent{.message_id = id,
                                       .role = ChatRole::User,
                                       .text = std::move(queued_content),
                                       .status = ChatMessageStatus::Queued}});
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

  EmitEvent(ChatEvent{ModelChangedEvent{.provider_id = std::move(provider_id),
                                        .model = std::move(new_model)}});
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
  tool_approval_->CancelPending();
}

void ChatService::ResolveToolApproval(std::string approval_id, bool approved) {
  tool_approval_->Resolve(approval_id, approved);
}

void ChatService::ResolveAskUser(const std::string& approval_id,
                                 std::string response) {
  tool_approval_->ResolveWithResponse(approval_id, true, std::move(response));
}

AgentMode ChatService::GetAgentMode() const {
  std::lock_guard lock(mutex_);
  return config_.agent_mode;
}

void ChatService::SetAgentMode(AgentMode mode) {
  {
    std::lock_guard lock(mutex_);
    if (config_.agent_mode == mode) {
      return;
    }
    config_.agent_mode = mode;
  }
  EmitEvent(ChatEvent{AgentModeChangedEvent{.mode = mode}});
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
  }
  sub_agent_manager_->CancelAll();
  tool_approval_->CancelPending();
  todo_state_.Clear();
  {
    std::lock_guard lock(mutex_);
    config_.agent_mode = AgentMode::Build;
  }
  wake_.notify_one();

  EmitEvent(ChatEvent{ConversationClearedEvent{}});
  EmitEvent(ChatEvent{AgentModeChangedEvent{.mode = AgentMode::Build}});
}

void ChatService::CompactConversation(decltype(sizeof(0)) keep_last) {
  {
    std::lock_guard lock(mutex_);
    if (active_ || !pending_.empty()) {
      return;
    }
    internal::CompactHistory(history_, keep_last);
  }

  EmitEvent(ChatEvent{ConversationCompactedEvent{}});
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

    EmitEvent(
        ChatEvent{UserMessageActiveEvent{.message_id = prompt.id,
                                         .role = ChatRole::User,
                                         .status = ChatMessageStatus::Active}});
    EmitQueueDepth();

    prompt_processor_->ProcessPrompt(prompt.id, prompt.content,
                                     generation_.load(),
                                     request_stop_source.get_token());

    {
      std::lock_guard lock(mutex_);
      active_ = false;
      active_stop_source_.reset();
    }
  }
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
  EmitEvent(ChatEvent{QueueDepthChangedEvent{.queue_depth = depth}});
}

ChatMessageId ChatService::NextMessageId() {
  return next_id_.fetch_add(1);
}

ChatConfig ChatService::ConfigSnapshot() const {
  std::lock_guard lock(mutex_);
  return config_;
}

std::string ChatService::SpawnBackgroundSubAgent(std::string task) {
  auto card_id = NextMessageId();
  std::string synthetic_tool_call_id = "user-task-" + std::to_string(card_id);

  // Seed the UI card via a synthetic ToolCallStarted event. Subsequent
  // SubAgentProgress/Completed events target the same card id.
  ::yac::tool_call::SubAgentCall preview{
      .task = task,
      .mode = ::yac::tool_call::SubAgentMode::Background,
      .status = ::yac::tool_call::SubAgentStatus::Running};
  EmitEvent(ChatEvent{ToolCallStartedEvent{
      .message_id = card_id,
      .role = ChatRole::Tool,
      .tool_call_id = synthetic_tool_call_id,
      .tool_name = std::string(::yac::tool_call::kSubAgentToolName),
      .tool_call = preview,
      .status = ChatMessageStatus::Active}});

  return sub_agent_manager_->SpawnBackground(task, card_id,
                                             std::move(synthetic_tool_call_id));
}

void ChatService::HandleBackgroundSubAgentResult(std::string tool_call_id,
                                                 std::string task,
                                                 std::string result,
                                                 bool is_error) {
  (void)tool_call_id;
  std::string header = is_error ? "[Background sub-agent failed]"
                                : "[Background sub-agent completed]";
  InjectSubAgentContinuation(header + " Task: " + task + "\n\nResult:\n" +
                             result);
}

void ChatService::InjectSubAgentContinuation(std::string body) {
  auto id = NextMessageId();
  auto queued_text = body;

  bool was_idle = false;
  {
    std::lock_guard lock(mutex_);
    was_idle = !active_ && pending_.empty();
    if (was_idle) {
      // Worker will append to history via AppendActiveUserMessage when it
      // picks the prompt up; don't push to history here to avoid duplicating.
      pending_.push_back({id, std::move(body)});
    } else {
      // Worker is busy on another turn; append directly to history so the
      // next request includes this continuation in context.
      ChatMessage message;
      message.id = id;
      message.role = ChatRole::User;
      message.status = ChatMessageStatus::Complete;
      message.content = std::move(body);
      history_.push_back(std::move(message));
    }
  }

  EmitEvent(
      ChatEvent{UserMessageQueuedEvent{.message_id = id,
                                       .role = ChatRole::User,
                                       .text = std::move(queued_text),
                                       .status = ChatMessageStatus::Complete,
                                       .role_label = kSubAgentRoleLabel}});

  if (was_idle) {
    wake_.notify_one();
    EmitQueueDepth();
  }
}

}  // namespace yac::chat
