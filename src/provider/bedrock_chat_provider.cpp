#include "provider/bedrock_chat_provider.hpp"

#include <utility>

namespace yac::provider {

struct BedrockChatProvider::Impl {
  chat::ProviderConfig config;
};

BedrockChatProvider::BedrockChatProvider(const chat::ProviderConfig& config)
    : impl_(std::make_unique<Impl>(Impl{config})) {}

BedrockChatProvider::~BedrockChatProvider() = default;

std::string BedrockChatProvider::Id() const {
  return "bedrock";
}

void BedrockChatProvider::CompleteStream(const chat::ChatRequest& request,
                                         ChatEventSink sink,
                                         std::stop_token stop_token) {
  (void)request;
  (void)stop_token;

  sink(chat::ChatEvent{
      chat::ErrorEvent{.text = "BedrockChatProvider not yet implemented"}});
  sink(chat::ChatEvent{chat::FinishedEvent{}});
}

bool BedrockChatProvider::SupportsModelDiscovery() const {
  return false;
}

int BedrockChatProvider::GetContextWindow(const std::string& model_id) const {
  (void)model_id;
  return 0;
}

}  // namespace yac::provider
