#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <memory>
#include <string>

namespace yac::provider {

class BedrockChatProvider final : public LanguageModelProvider {
 public:
  explicit BedrockChatProvider(const chat::ProviderConfig& config);
  ~BedrockChatProvider() override;

  BedrockChatProvider(const BedrockChatProvider&) = delete;
  BedrockChatProvider& operator=(const BedrockChatProvider&) = delete;

  [[nodiscard]] std::string Id() const override;
  void CompleteStream(const chat::ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override;
  [[nodiscard]] bool SupportsModelDiscovery() const override;
  [[nodiscard]] int GetContextWindow(const std::string& model_id) const override;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace yac::provider
