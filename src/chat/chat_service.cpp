#include "chat/chat_service.hpp"

#include "chat/chat_service_prompt_processor.hpp"
#include "chat/chat_service_request_builder.hpp"
#include "chat/chat_service_tool_approval.hpp"

#include <memory>
#include <utility>

namespace yac::chat {

ChatService::ChatService(provider::ProviderRegistry registry, ChatConfig config)
    : registry_(std::move(registry)),
      config_(std::move(config)),
      tool_executor_(internal::MakeChatToolExecutor(config_)),
      tool_approval_(std::make_unique<internal::ChatServiceToolApproval>()),
      sub_agent_manager_(std::make_unique<SubAgentManager>(
          registry_, tool_executor_, *tool_approval_,
          [this](ChatEvent event) { EmitEvent(std::move(event)); },
          [this] { return ConfigSnapshot(); },
          [this] { return NextMessageId(); })),
      prompt_processor_(std::make_unique<internal::ChatServicePromptProcessor>(
          registry_, *tool_executor_, *tool_approval_, mutex_, history_,
          [this](ChatEvent event) { EmitEvent(std::move(event)); },
          [this] { return NextMessageId(); },
          [this] { return ConfigSnapshot(); },
          [this] { return generation_.load(); }, std::set<std::string>{},
          sub_agent_manager_->GetApprovalGate())) {
  tool_executor_->SetSubAgentManager(sub_agent_manager_.get());
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
  tool_approval_->CancelPending();
}

void ChatService::ResolveToolApproval(std::string approval_id, bool approved) {
  tool_approval_->Resolve(approval_id, approved);
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
  wake_.notify_one();

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

}  // namespace yac::chat
