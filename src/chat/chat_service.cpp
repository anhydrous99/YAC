#include "chat/chat_service.hpp"

#include <utility>

namespace yac::chat {

ChatService::ChatService(provider::ProviderRegistry registry, ChatConfig config)
    : registry_(std::move(registry)), config_(std::move(config)) {
  worker_ = std::jthread([this](std::stop_token st) { WorkerLoop(st); });
}

ChatService::~ChatService() {
  worker_.request_stop();
  wake_.notify_one();
}

void ChatService::SetEventCallback(ChatEventCallback callback) {
  std::lock_guard lock(mutex_);
  callback_ = std::move(callback);
}

ChatMessageId ChatService::SubmitUserMessage(std::string content) {
  auto id = NextMessageId();
  {
    std::lock_guard lock(mutex_);
    pending_.push_back({id, std::move(content)});
  }
  wake_.notify_one();

  EmitEvent(ChatEvent{.type = ChatEventType::UserMessageQueued,
                      .message_id = id,
                      .status = ChatMessageStatus::Queued});
  EmitQueueDepth();
  return id;
}

void ChatService::CancelActiveResponse() {
  std::lock_guard lock(mutex_);
  if (!active_) {
    return;
  }
  generation_.fetch_add(1);
  active_ = false;
}

void ChatService::ResetConversation() {
  uint64_t old_gen = generation_.fetch_add(1);
  (void)old_gen;

  {
    std::lock_guard lock(mutex_);
    history_.clear();
    pending_.clear();
    active_ = false;
  }
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
    }

    EmitEvent(ChatEvent{.type = ChatEventType::UserMessageActive,
                        .message_id = prompt.id,
                        .status = ChatMessageStatus::Active});
    EmitQueueDepth();

    ProcessPrompt(prompt, generation_.load());

    {
      std::lock_guard lock(mutex_);
      active_ = false;
    }
  }
}

void ChatService::ProcessPrompt(const PendingPrompt& prompt,
                                uint64_t generation) {
  auto provider = registry_.Resolve(config_.provider_id);
  if (provider == nullptr) {
    EmitEvent(ChatEvent{
        .type = ChatEventType::Error,
        .message_id = prompt.id,
        .text = "No provider registered for '" + config_.provider_id + "'."});
    EmitEvent(
        ChatEvent{.type = ChatEventType::Finished, .message_id = prompt.id});
    return;
  }

  ChatRequest request = BuildRequest();
  {
    std::lock_guard lock(mutex_);
    history_.push_back(ChatMessage{.id = prompt.id,
                                   .role = ChatRole::User,
                                   .status = ChatMessageStatus::Active,
                                   .content = prompt.content});
    if (config_.system_prompt.has_value()) {
      request.messages.insert(request.messages.begin(),
                              ChatMessage{.role = ChatRole::System,
                                          .status = ChatMessageStatus::Complete,
                                          .content = *config_.system_prompt});
    }
    request.messages = history_;
  }

  EmitEvent(ChatEvent{.type = ChatEventType::Started,
                      .message_id = prompt.id,
                      .provider_id = config_.provider_id,
                      .model = config_.model});

  std::string assistant_text;
  auto sink = [this, &assistant_text, prompt_id = prompt.id,
               generation](ChatEvent event) mutable {
    if (generation_.load() != generation) {
      return;
    }
    event.message_id = prompt_id;
    if (event.type == ChatEventType::TextDelta) {
      assistant_text += event.text;
    }
    EmitEvent(std::move(event));
  };

  provider->CompleteStream(request, std::move(sink), {});

  if (generation_.load() != generation) {
    EmitEvent(ChatEvent{.type = ChatEventType::MessageStatusChanged,
                        .message_id = prompt.id,
                        .status = ChatMessageStatus::Cancelled});
    EmitEvent(
        ChatEvent{.type = ChatEventType::Finished, .message_id = prompt.id});
    return;
  }

  if (!assistant_text.empty()) {
    {
      std::lock_guard lock(mutex_);
      history_.push_back(ChatMessage{.id = NextMessageId(),
                                     .role = ChatRole::Assistant,
                                     .status = ChatMessageStatus::Complete,
                                     .content = assistant_text});
    }
    EmitEvent(ChatEvent{.type = ChatEventType::AssistantMessageDone,
                        .message_id = prompt.id});
  }
  EmitEvent(
      ChatEvent{.type = ChatEventType::Finished, .message_id = prompt.id});
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

ChatRequest ChatService::BuildRequest() const {
  ChatRequest request;
  request.provider_id = config_.provider_id;
  request.model = config_.model;
  request.temperature = config_.temperature;
  request.stream = true;
  return request;
}

}  // namespace yac::chat
