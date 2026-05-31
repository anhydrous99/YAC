#pragma once

#include "provider/openai_auth_flow.hpp"
#include "provider/openai_compatible_chat_provider.hpp"

#include <memory>
#include <optional>
#include <string>

namespace yac::provider {

class OpenAiChatProvider : public OpenAiCompatibleChatProvider {
 public:
  enum class EffectiveAuthSource {
    None,
    EnvApiKey,
    StoredApiKey,
    StoredOAuth,
    InlineApiKey,
    StateStoreApiKey,
    StateStoreOAuth,
  };

  struct Dependencies {
    std::shared_ptr<OpenAiAuthFlow> auth_flow;
    std::string oauth_base_url;
  };

  explicit OpenAiChatProvider(chat::ProviderConfig config = {});
  OpenAiChatProvider(chat::ProviderConfig config, Dependencies dependencies);

  [[nodiscard]] std::vector<chat::ModelInfo> ListModels(
      std::chrono::milliseconds timeout) override;
  void CompleteStream(const chat::ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override;
  [[nodiscard]] static bool IsOAuthModelAllowed(std::string_view model_id);
  [[nodiscard]] EffectiveAuthSource ResolveEffectiveAuthSource() const;

 protected:
  [[nodiscard]] std::string ResolveApiKey() const override;

 private:
  struct EffectiveAuth {
    std::string api_key;
    std::optional<OpenAiOAuthAuth> oauth;
  };

  [[nodiscard]] EffectiveAuth ResolveEffectiveAuth() const;
  [[nodiscard]] std::optional<OpenAiAuth> LoadStateStoreOpenAiAuth() const;
  [[nodiscard]] static std::vector<chat::ModelInfo> OAuthModelAllowlist();
  void CompleteWithOAuth(const chat::ChatRequest& request, ChatEventSink sink,
                         std::stop_token stop_token,
                         const OpenAiOAuthAuth& auth) const;

  chat::ProviderConfig config_;
  std::shared_ptr<OpenAiAuthFlow> auth_flow_;
  std::string oauth_base_url_;
};

}  // namespace yac::provider
