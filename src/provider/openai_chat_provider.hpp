#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <string>

namespace yac::provider {

class OpenAiChatProvider : public LanguageModelProvider {
 public:
  explicit OpenAiChatProvider(chat::ProviderConfig config = {});

  [[nodiscard]] std::string Id() const override;
  void CompleteStream(const chat::ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override;

  [[nodiscard]] static std::string RoleToOpenAi(chat::ChatRole role);
  [[nodiscard]] static chat::ChatEvent ParseStreamData(const std::string& data);

 private:
  void CompleteBuffered(const chat::ChatRequest& request, ChatEventSink sink);
  void CompleteStreaming(const chat::ChatRequest& request, ChatEventSink sink,
                         std::stop_token stop_token);
  [[nodiscard]] std::string ResolveApiKey() const;
  [[nodiscard]] std::string CompletionUrl() const;

  chat::ProviderConfig config_;
};

}  // namespace yac::provider
