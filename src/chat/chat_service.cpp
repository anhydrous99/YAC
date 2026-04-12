#include "chat/chat_service.hpp"

#include <utility>

namespace yac::chat {

ChatService::ChatService(provider::ProviderRegistry registry,
                         std::string provider_id, std::string model)
    : registry_(std::move(registry)),
      provider_id_(std::move(provider_id)),
      model_(std::move(model)) {}

ChatService::~ChatService() {
  CancelAll();
}

void ChatService::SubmitUserMessage(std::string content,
                                    ChatEventCallback callback) {
  auto provider = registry_.Resolve(provider_id_);
  if (provider == nullptr) {
    callback(ChatEvent{
        .type = ChatEventType::Error,
        .text = "No provider registered for '" + provider_id_ + "'."});
    callback(ChatEvent{.type = ChatEventType::Finished});
    return;
  }

  ChatRequest request;
  request.provider_id = provider_id_;
  request.model = model_;
  request.stream = true;

  {
    std::lock_guard lock(mutex_);
    history_.push_back(
        ChatMessage{.role = ChatRole::User, .content = std::move(content)});
    request.messages = history_;
  }

  requests_.emplace_back(
      [this, provider, request = std::move(request),
       callback = std::move(callback)](std::stop_token stop_token) mutable {
        callback(ChatEvent{.type = ChatEventType::Started,
                           .provider_id = request.provider_id,
                           .model = request.model});

        std::string assistant_text;
        auto sink = [this, &assistant_text, callback](ChatEvent event) mutable {
          if (event.type == ChatEventType::TextDelta) {
            assistant_text += event.text;
          }
          callback(event);
        };

        provider->CompleteStream(request, std::move(sink), stop_token);

        if (stop_token.stop_requested()) {
          callback(ChatEvent{.type = ChatEventType::Cancelled});
          return;
        }
        if (!assistant_text.empty()) {
          RecordAssistantMessage(assistant_text);
          callback(ChatEvent{.type = ChatEventType::AssistantMessageDone});
        }
        callback(ChatEvent{.type = ChatEventType::Finished});
      });
}

void ChatService::CancelAll() {
  for (auto& request : requests_) {
    request.request_stop();
  }
  requests_.clear();
}

std::vector<ChatMessage> ChatService::History() const {
  std::lock_guard lock(mutex_);
  return history_;
}

void ChatService::RecordAssistantMessage(const std::string& content) {
  std::lock_guard lock(mutex_);
  history_.push_back(
      ChatMessage{.role = ChatRole::Assistant, .content = content});
}

}  // namespace yac::chat
