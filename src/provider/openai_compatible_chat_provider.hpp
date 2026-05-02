#pragma once

#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
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
  // Lookup chain: discovered cache (populated by `ListModels`) → built-in
  // z.ai preset table when `config_.id == "zai"` → 0.
  [[nodiscard]] int GetContextWindow(
      const std::string& model_id) const override;
  void CompleteStream(const chat::ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override;

  [[nodiscard]] static std::string RoleToOpenAi(chat::ChatRole role);
  [[nodiscard]] static std::vector<chat::ModelInfo> ParseModelsData(
      const std::string& data);
  [[nodiscard]] static chat::ChatEvent ParseStreamData(const std::string& data);
  [[nodiscard]] static std::optional<chat::TokenUsage> ParseUsageJson(
      const std::string& data);

  // Test seam: install a discovered window without performing HTTP. Lets
  // unit tests exercise the cache path of `GetContextWindow` directly.
  void SeedDiscoveredContextWindowForTest(const std::string& model_id,
                                          int context_window);

 protected:
  [[nodiscard]] virtual std::string ResolveApiKey() const;

 private:
  void CompleteBuffered(const chat::ChatRequest& request, ChatEventSink sink);
  void CompleteStreaming(const chat::ChatRequest& request, ChatEventSink sink,
                         std::stop_token stop_token);
  [[nodiscard]] std::string CompletionUrl() const;
  [[nodiscard]] std::string ModelsUrl() const;
  void StoreDiscoveredContextWindows(
      const std::vector<chat::ModelInfo>& models);

  chat::ProviderConfig config_;
  mutable std::mutex discovered_windows_mutex_;
  std::unordered_map<std::string, int> discovered_windows_;
};

}  // namespace yac::provider
