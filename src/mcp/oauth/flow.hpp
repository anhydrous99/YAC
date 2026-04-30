#pragma once

#include <chrono>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yac::mcp::oauth {

struct OAuthInteractionMode {
  bool browser_disabled = false;
  std::optional<std::string> injected_callback_url;
};

struct OAuthTokens {
  std::string access_token;
  std::string refresh_token;
  std::chrono::system_clock::time_point expires_at;
  std::string token_type;
  std::string scope;
};

struct OAuthConfig {
  std::string authorization_url;
  std::string token_url;
  std::string client_id;
  std::vector<std::string> scopes;
  std::string resource_url;
};

struct OAuthDiscoveryResult {
  std::string authorization_endpoint;
  std::string token_endpoint;
  std::vector<std::string> code_challenge_methods_supported;
};

class OAuthFlow {
 public:
  OAuthFlow() = default;

  [[nodiscard]] std::string BuildAuthorizationUrl(
      const OAuthConfig& config, std::string_view code_challenge,
      std::string_view state, std::string_view redirect_uri,
      std::string_view resource);

  [[nodiscard]] OAuthTokens ExchangeCode(const OAuthConfig& config,
                                         std::string_view code,
                                         std::string_view code_verifier,
                                         std::string_view redirect_uri,
                                         std::string_view resource);

  [[nodiscard]] OAuthTokens RefreshToken(const OAuthConfig& config,
                                         std::string_view refresh_token,
                                         std::string_view resource);

  void ValidateState(std::string_view callback_state);

 private:
  enum class State {
    Idle,
    AwaitingCallback,
    Authorized,
  };

  [[nodiscard]] static OAuthTokens PerformTokenRequest(
      const OAuthConfig& config,
      const std::vector<std::pair<std::string, std::string>>& form_fields);

  std::mutex mutex_;
  std::condition_variable refresh_cv_;
  State state_ = State::Idle;
  std::string expected_state_;
  bool state_validated_ = false;
  bool refresh_in_progress_ = false;
  std::optional<OAuthTokens> refresh_result_;
  std::optional<std::string> refresh_error_;
};

}  // namespace yac::mcp::oauth
