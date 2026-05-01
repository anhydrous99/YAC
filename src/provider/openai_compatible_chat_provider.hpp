#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <chrono>
#include <optional>
#include <string>
#include <vector>

namespace yac::provider {

// Generic provider that speaks the OpenAI Chat Completions wire protocol
// against any compatible endpoint configured via `chat::ProviderConfig`.
// Subclass and override `ResolveApiKey()` to plug in custom auth (e.g. an
// OAuth-issued access token); the rest of the HTTP/SSE plumbing is reused.
class OpenAiCompatibleChatProvider : public LanguageModelProvider {
 public:
  explicit OpenAiCompatibleChatProvider(chat::ProviderConfig config = {});

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
  [[nodiscard]] static std::optional<chat::TokenUsage> ParseUsageJson(
      const std::string& data);

 protected:
  [[nodiscard]] virtual std::string ResolveApiKey() const;

 private:
  void CompleteBuffered(const chat::ChatRequest& request, ChatEventSink sink);
  void CompleteStreaming(const chat::ChatRequest& request, ChatEventSink sink,
                         std::stop_token stop_token);
  [[nodiscard]] std::string CompletionUrl() const;
  [[nodiscard]] std::string ModelsUrl() const;

  chat::ProviderConfig config_;
};

}  // namespace yac::provider
