#pragma once

#include "chat/types.hpp"
#include "provider/openai_auth_flow.hpp"
#include "provider/openai_auth_store.hpp"

#include <filesystem>
#include <functional>
#include <istream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yac::cli {

struct ProviderAuthStatusSummary {
  std::string configured_provider;
  std::optional<std::string> stored_credential;
  std::optional<std::string> stored_source;
  std::optional<std::string> effective_auth;
  std::optional<std::string> oauth_expiry;
  std::optional<std::string> account_id;
  std::vector<std::string> warnings;
};

struct OpenAiLoginResult {
  bool browser_launched = true;
  std::optional<std::string> authorization_url;
  std::optional<std::string> verification_url;
  std::optional<std::string> user_code;
};

class ProviderAuthCommand {
 public:
  using LoginFn = std::function<provider::OpenAiOAuthAuth(
      const provider::OpenAiAuthorizationObserver&)>;
  using DeviceLoginFn = std::function<provider::OpenAiOAuthAuth(
      const provider::OpenAiDeviceAuthorizationObserver&)>;
  using EnvLookupFn =
      std::function<std::optional<std::string>(std::string_view)>;
  using LoadConfigFn =
      std::function<chat::ChatConfigResult(const std::filesystem::path&)>;

  struct Options {
    std::filesystem::path settings_path;
    std::shared_ptr<provider::OpenAiAuthStore> auth_store;
    LoginFn login_fn;
    DeviceLoginFn device_login_fn;
    EnvLookupFn env_lookup;
    LoadConfigFn load_config;
    std::istream* in = nullptr;
  };

  ProviderAuthCommand();
  explicit ProviderAuthCommand(Options opts);

  [[nodiscard]] OpenAiLoginResult LoginOpenAi(
      const provider::OpenAiAuthorizationObserver& observer = {});
  [[nodiscard]] OpenAiLoginResult LoginOpenAiDevice(
      const provider::OpenAiDeviceAuthorizationObserver& observer = {});
  [[nodiscard]] provider::OpenAiAuthStorageSource SetOpenAiApiKeyFromStdin();
  [[nodiscard]] ProviderAuthStatusSummary GetOpenAiStatus() const;
  [[nodiscard]] ProviderAuthStatusSummary LogoutOpenAi();

 private:
  struct ConfigSecrets {
    chat::ChatConfigResult config_result;
    std::optional<std::string> env_api_key;
    std::optional<std::string> inline_api_key;
  };

  [[nodiscard]] std::filesystem::path ResolveSettingsPath() const;
  [[nodiscard]] ConfigSecrets LoadConfigSecrets() const;
  [[nodiscard]] static ProviderAuthStatusSummary BuildStatusSummary(
      std::optional<provider::StoredOpenAiAuth> stored_auth,
      const ConfigSecrets& config_secrets, bool after_logout);

  Options opts_;
  std::shared_ptr<provider::OpenAiAuthStore> auth_store_;
};

}  // namespace yac::cli
