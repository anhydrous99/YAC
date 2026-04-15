#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <chrono>
#include <string>
#include <vector>

namespace yac::provider {

class OpenAiChatProvider : public LanguageModelProvider {
 public:
  explicit OpenAiChatProvider(chat::ProviderConfig config = {});

  [[nodiscard]] std::string Id() const override;
  [[nodiscard]] bool SupportsModelDiscovery() const override { return true; }
  [[nodiscard]] std::vector<chat::ModelInfo> ListModels(
      std::chrono::milliseconds timeout) override;
  void CompleteStream(const chat::ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override;

  [[nodiscard]] static std::string RoleToOpenAi(chat::ChatRole role);
  [[nodiscard]] static std::vector<chat::ModelInfo> ParseModelsData(
      const std::string& data);
  [[nodiscard]] static chat::ChatEvent ParseStreamData(const std::string& data);

 private:
  void CompleteBuffered(const chat::ChatRequest& request, ChatEventSink sink);
  void CompleteStreaming(const chat::ChatRequest& request, ChatEventSink sink,
                         std::stop_token stop_token);
  [[nodiscard]] std::string ResolveApiKey() const;
  [[nodiscard]] std::string CompletionUrl() const;
  [[nodiscard]] std::string ModelsUrl() const;

  chat::ProviderConfig config_;
};

}  // namespace yac::provider
