#include "chat/chat_service.hpp"

#include <utility>

namespace yac::chat {

ChatService::ChatService(provider::ProviderRegistry registry, ChatConfig config)
    : registry_(std::move(registry)), config_(std::move(config)) {
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

  ChatRequest request = BuildRequest(config);
  {
    std::lock_guard lock(mutex_);
    history_.push_back(ChatMessage{.id = prompt.id,
                                   .role = ChatRole::User,
                                   .status = ChatMessageStatus::Active,
                                   .content = prompt.content});
    request.messages = history_;
    if (config.system_prompt.has_value()) {
      request.messages.insert(request.messages.begin(),
                              ChatMessage{.role = ChatRole::System,
                                          .status = ChatMessageStatus::Complete,
                                          .content = *config.system_prompt});
    }
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

  std::string assistant_text;
  bool assistant_error = false;
  auto sink = [this, &assistant_text, assistant_id, generation,
               &assistant_error](ChatEvent event) mutable {
    if (generation_.load() != generation) {
      return;
    }
    event.message_id = assistant_id;
    event.role = ChatRole::Assistant;
    if (event.type == ChatEventType::TextDelta) {
      assistant_text += event.text;
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
    EmitEvent(
        ChatEvent{.type = ChatEventType::Finished, .message_id = assistant_id});
    return;
  }

  if (assistant_error) {
    EmitEvent(
        ChatEvent{.type = ChatEventType::Finished, .message_id = assistant_id});
    return;
  }

  if (!assistant_text.empty()) {
    {
      std::lock_guard lock(mutex_);
      history_.push_back(ChatMessage{.id = assistant_id,
                                     .role = ChatRole::Assistant,
                                     .status = ChatMessageStatus::Complete,
                                     .content = assistant_text});
    }
  }
  EmitEvent(ChatEvent{.type = ChatEventType::AssistantMessageDone,
                      .message_id = assistant_id,
                      .role = ChatRole::Assistant,
                      .status = ChatMessageStatus::Complete});
  EmitEvent(
      ChatEvent{.type = ChatEventType::Finished, .message_id = assistant_id});
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
