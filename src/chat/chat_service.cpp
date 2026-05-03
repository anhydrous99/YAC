#include "chat/chat_service.hpp"

#include "chat/agent_mode.hpp"
#include "chat/chat_service_history.hpp"
#include "chat/chat_service_prompt_processor.hpp"
#include "chat/chat_service_request_builder.hpp"
#include "chat/chat_service_tool_approval.hpp"

#include <algorithm>
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
          [this] { return ExcludedToolsForMode(config_.agent_mode); },
          internal::ChatServicePromptProcessor::PrepareBuiltInToolCallFn{},
          internal::ChatServicePromptProcessor::ExecuteBuiltInToolCallFn{},
          [this](const TokenUsage& usage) {
            std::scoped_lock lock(mutex_);
            last_usage_ = usage;
          },
          [this] { return LastUsage(); })) {
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
    std::scoped_lock lock(mutex_);
    if (active_stop_source_.has_value()) {
      active_stop_source_->request_stop();
    }
  }
  tool_approval_->CancelPending();
  worker_.request_stop();
  wake_.notify_all();
  if (worker_.joinable()) {
    worker_.join();
  }
  sub_agent_manager_.reset();
}

void ChatService::SetEventCallback(ChatEventCallback callback) {
  std::scoped_lock lock(mutex_);
  callback_ = std::move(callback);
}

ChatMessageId ChatService::SubmitUserMessage(std::string content) {
  auto id = NextMessageId();
  auto queued_content = content;
  {
    std::scoped_lock lock(mutex_);
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
    std::scoped_lock lock(mutex_);
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
  std::scoped_lock lock(mutex_);
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
  std::scoped_lock lock(mutex_);
  return config_.agent_mode;
}

void ChatService::SetAgentMode(AgentMode mode) {
  {
    std::scoped_lock lock(mutex_);
    if (config_.agent_mode == mode) {
      return;
    }
    config_.agent_mode = mode;
  }
  EmitEvent(ChatEvent{AgentModeChangedEvent{.mode = mode}});
}

void ChatService::ResetConversation() {
  // Bump generation up front so the worker's ProcessPrompt observes
  // cancellation at every guarded site. Then signal the active prompt
  // to stop and drop any queued prompts so they don't get processed
  // under the new generation.
  generation_.fetch_add(1);
  {
    std::scoped_lock lock(mutex_);
    pending_.clear();
    if (active_stop_source_.has_value()) {
      active_stop_source_->request_stop();
    }
  }

  // Wait for the worker to finish whatever ProcessPrompt is in flight.
  // The worker notifies wake_ after setting active_=false. Bounded so a
  // misbehaving provider that ignores stop_token can't pin the UI thread;
  // if the budget expires, the per-append generation guards in
  // ProcessPrompt prevent the worker from corrupting the cleared
  // history that follows.
  {
    std::unique_lock lock(mutex_);
    const auto budget = std::chrono::milliseconds(
        reset_drain_budget_ms_.load(std::memory_order_relaxed));
    wake_.wait_for(lock, budget, [this] { return !active_; });
  }

  // Now safe to mutate history. We do not touch active_ or
  // active_stop_source_ — those are owned by the worker thread.
  //
  // Bump generation a second time, under the lock, before clearing
  // history. This closes a window where a worker that captured the
  // first post-bump generation has already passed its append guard
  // (so history_ has its user_msg) but hasn't yet entered
  // BuildRoundRequest. Without the second bump, a budget-expired
  // wait_for above would let us clear history while the worker still
  // sees a matching generation — the next BuildRoundRequest would
  // then issue an empty messages vector to the provider.
  {
    std::scoped_lock lock(mutex_);
    generation_.fetch_add(1);
    history_.clear();
    last_usage_.reset();
    config_.agent_mode = AgentMode::Build;
  }

  sub_agent_manager_->CancelAll();
  tool_approval_->CancelPending();
  todo_state_.Clear();

  EmitEvent(ChatEvent{ConversationClearedEvent{}});
  EmitEvent(ChatEvent{AgentModeChangedEvent{.mode = AgentMode::Build}});
}

void ChatService::SetResetDrainBudgetForTest(std::chrono::milliseconds budget) {
  reset_drain_budget_ms_.store(std::max<int64_t>(1, budget.count()),
                               std::memory_order_relaxed);
}

void ChatService::CompactConversation(decltype(sizeof(0)) keep_last) {
  int messages_removed = 0;
  {
    std::scoped_lock lock(mutex_);
    if (active_ || !pending_.empty()) {
      return;
    }
    const auto before_non_system = static_cast<decltype(sizeof(0))>(
        std::ranges::count_if(history_, [](const ChatMessage& message) {
          return message.role != ChatRole::System;
        }));
    internal::CompactHistory(history_, keep_last);
    if (before_non_system > keep_last) {
      messages_removed = static_cast<int>(before_non_system - keep_last);
    }
  }

  EmitEvent(ChatEvent{ConversationCompactedEvent{
      .reason = CompactReason::Manual,
      .messages_removed = messages_removed,
  }});
}

std::vector<ChatMessage> ChatService::History() const {
  std::scoped_lock lock(mutex_);
  return history_;
}

bool ChatService::IsBusy() const {
  std::scoped_lock lock(mutex_);
  return active_ || !pending_.empty();
}

int ChatService::QueueDepth() const {
  std::scoped_lock lock(mutex_);
  return static_cast<int>(pending_.size());
}

std::optional<TokenUsage> ChatService::LastUsage() const {
  std::scoped_lock lock(mutex_);
  return last_usage_;
}

void ChatService::WorkerLoop(std::stop_token stop_token) {
  // libc++ < 18.1 (llvm/llvm-project#76807) does not wake the stop_token-aware
  // wait overload on request_stop; route around it with an explicit
  // stop_callback that triggers notify_all and a predicate-form wait that
  // folds stop_requested() into the predicate.
  std::stop_callback wake_on_stop(stop_token, [this] { wake_.notify_all(); });
  while (!stop_token.stop_requested()) {
    PendingPrompt prompt;
    std::stop_source request_stop_source;
    {
      std::unique_lock lock(mutex_);
      wake_.wait(lock, [&] {
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
      std::scoped_lock lock(mutex_);
      active_ = false;
      active_stop_source_.reset();
    }
    // Wake any thread (notably ResetConversation) waiting for this prompt
    // to drain. Cheap; the wait predicate filters spurious wakes.
    wake_.notify_all();
  }
}

void ChatService::EmitEvent(ChatEvent event) const {
  ChatEventCallback cb;
  {
    std::scoped_lock lock(mutex_);
    cb = callback_;
  }
  if (cb) {
    cb(std::move(event));
  }
}

void ChatService::EmitQueueDepth() {
  int depth = 0;
  {
    std::scoped_lock lock(mutex_);
    depth = static_cast<int>(pending_.size());
  }
  EmitEvent(ChatEvent{QueueDepthChangedEvent{.queue_depth = depth}});
}

ChatMessageId ChatService::NextMessageId() {
  return next_id_.fetch_add(1);
}

ChatConfig ChatService::ConfigSnapshot() const {
  std::scoped_lock lock(mutex_);
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
    std::scoped_lock lock(mutex_);
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
