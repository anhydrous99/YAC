#pragma once

#include "provider/openai_auth_store.hpp"

#include <chrono>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <string_view>

namespace yac::provider {

struct OpenAiAuthorizationNotice {
  std::string authorization_url;
  std::string redirect_uri;
  bool browser_launched = false;
};

using OpenAiAuthorizationObserver =
    std::function<void(const OpenAiAuthorizationNotice&)>;

class OpenAiAuthFlow {
 public:
  struct TokenResponse {
    std::string access_token;
    std::optional<std::string> refresh_token;
    std::optional<std::string> id_token;
    std::optional<std::chrono::system_clock::time_point> expires_at;
  };

  struct Dependencies {
    std::string issuer_url = "https://auth.openai.com";
    std::function<bool(std::string_view)> browser_launcher;
    std::function<std::chrono::system_clock::time_point()> clock;
    std::function<std::string()> code_verifier_generator;
    std::function<std::string(std::string_view)> code_challenge_deriver;
    std::function<std::string()> state_generator;
    std::shared_ptr<OpenAiAuthStore> auth_store;
  };

  OpenAiAuthFlow();
  explicit OpenAiAuthFlow(Dependencies dependencies);

  [[nodiscard]] std::optional<StoredOpenAiAuth> LoadStoredAuth() const;
  [[nodiscard]] OpenAiOAuthAuth RunBrowserAuthorization(
      const OpenAiAuthorizationObserver& observer = {},
      std::stop_token stop_token = {});
  [[nodiscard]] OpenAiOAuthAuth RefreshIfNeeded(const OpenAiOAuthAuth& auth);
  [[nodiscard]] OpenAiOAuthAuth Refresh(const OpenAiOAuthAuth& auth);

 private:
  [[nodiscard]] std::string BuildAuthorizationUrl(
      std::string_view redirect_uri, std::string_view code_challenge,
      std::string_view state) const;
  static void ValidateCallbackState(std::string_view callback_state,
                                    std::string_view expected_state);
  [[nodiscard]] TokenResponse ExchangeAuthorizationCode(
      std::string_view code, std::string_view code_verifier,
      std::string_view redirect_uri) const;
  [[nodiscard]] TokenResponse RefreshToken(
      std::string_view refresh_token) const;
  [[nodiscard]] OpenAiOAuthAuth PersistTokenResponse(
      const OpenAiOAuthAuth& prior_auth, const TokenResponse& response) const;
  [[nodiscard]] OpenAiOAuthAuth RefreshLocked(const OpenAiOAuthAuth& auth);

  Dependencies dependencies_;
  mutable std::mutex refresh_mutex_;
};

}  // namespace yac::provider
