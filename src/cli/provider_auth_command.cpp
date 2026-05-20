#include "cli/provider_auth_command.hpp"

#include "chat/config.hpp"
#include "chat/config_paths.hpp"

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <utility>

namespace yac::cli {

namespace {

std::optional<std::string> LookupEnv(std::string_view name) {
  const std::string key(name);
  const char* value = std::getenv(key.c_str());
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  return std::string(value);
}

std::optional<std::string> ReadInlineApiKey(
    const std::filesystem::path& settings_path) {
  std::ifstream input(settings_path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream content;
  content << input.rdbuf();
  if (content.str().empty()) {
    return std::nullopt;
  }

  try {
    auto table = toml::parse(content.str());
    const auto provider = table["provider"];
    if (!provider.is_table()) {
      return std::nullopt;
    }
    if (const auto* value = (*provider.as_table())["api_key"].as_string()) {
      const std::string api_key = value->get();
      if (!api_key.empty()) {
        return api_key;
      }
    }
  } catch (const std::exception& error) {
    (void)error;
  }
  return std::nullopt;
}

std::string OauthExpiryValue(std::chrono::system_clock::time_point expires_at) {
  return std::to_string(std::chrono::duration_cast<std::chrono::seconds>(
                            expires_at.time_since_epoch())
                            .count());
}

std::string StoredCredentialLabel(const provider::OpenAiAuth& auth) {
  return std::holds_alternative<provider::OpenAiApiKeyAuth>(auth) ? "api"
                                                                  : "oauth";
}

}  // namespace

ProviderAuthCommand::ProviderAuthCommand() : ProviderAuthCommand(Options{}) {}

ProviderAuthCommand::ProviderAuthCommand(Options opts)
    : opts_(std::move(opts)) {
  auth_store_ = opts_.auth_store != nullptr
                    ? opts_.auth_store
                    : std::make_shared<provider::OpenAiAuthStore>();
  if (!opts_.env_lookup) {
    opts_.env_lookup = LookupEnv;
  }
  if (!opts_.load_config) {
    opts_.load_config = [](const std::filesystem::path& settings_path) {
      return chat::LoadChatConfigResultFrom(settings_path,
                                            /*create_if_missing=*/true);
    };
  }
  if (!opts_.login_fn) {
    auto flow = std::make_shared<provider::OpenAiAuthFlow>(
        provider::OpenAiAuthFlow::Dependencies{.auth_store = auth_store_});
    opts_.login_fn =
        [flow](const provider::OpenAiAuthorizationObserver& observer) {
          return flow->RunBrowserAuthorization(observer);
        };
  }
}

OpenAiLoginResult ProviderAuthCommand::LoginOpenAi(
    const provider::OpenAiAuthorizationObserver& observer) {
  OpenAiLoginResult result;
  const provider::OpenAiOAuthAuth auth = opts_.login_fn(
      [&result, &observer](const provider::OpenAiAuthorizationNotice& notice) {
        result.browser_launched = notice.browser_launched;
        if (!notice.browser_launched) {
          result.authorization_url = notice.authorization_url;
        }
        if (observer) {
          observer(notice);
        }
      });
  if (!auth_store_->Load().has_value()) {
    static_cast<void>(auth_store_->Save(auth));
  }
  return result;
}

provider::OpenAiAuthStorageSource
ProviderAuthCommand::SetOpenAiApiKeyFromStdin() {
  std::istream& input = opts_.in != nullptr ? *opts_.in : std::cin;
  std::string api_key;
  if (!std::getline(input, api_key)) {
    throw std::runtime_error("set-api-key --stdin requires one line on stdin");
  }
  if (api_key.empty()) {
    throw std::runtime_error("API key must not be empty");
  }
  return auth_store_->Save(
      provider::OpenAiApiKeyAuth{.key = std::move(api_key)});
}

ProviderAuthStatusSummary ProviderAuthCommand::GetOpenAiStatus() const {
  const ConfigSecrets config_secrets = LoadConfigSecrets();
  return BuildStatusSummary(auth_store_->Load(), config_secrets,
                            /*after_logout=*/false);
}

ProviderAuthStatusSummary ProviderAuthCommand::LogoutOpenAi() {
  auth_store_->Erase();
  const ConfigSecrets config_secrets = LoadConfigSecrets();
  return BuildStatusSummary(std::nullopt, config_secrets,
                            /*after_logout=*/true);
}

std::filesystem::path ProviderAuthCommand::ResolveSettingsPath() const {
  if (!opts_.settings_path.empty()) {
    return opts_.settings_path;
  }
  return chat::GetSettingsPath();
}

ProviderAuthCommand::ConfigSecrets ProviderAuthCommand::LoadConfigSecrets()
    const {
  const std::filesystem::path settings_path = ResolveSettingsPath();
  ConfigSecrets result;
  result.config_result = opts_.load_config(settings_path);
  result.env_api_key =
      opts_.env_lookup(result.config_result.config.api_key_env);
  result.inline_api_key = ReadInlineApiKey(settings_path);
  return result;
}

ProviderAuthStatusSummary ProviderAuthCommand::BuildStatusSummary(
    std::optional<provider::StoredOpenAiAuth> stored_auth,
    const ConfigSecrets& config_secrets, bool after_logout) {
  ProviderAuthStatusSummary summary;
  summary.configured_provider =
      config_secrets.config_result.config.provider_id.value;
  const bool is_openai_provider = summary.configured_provider == "openai";
  const bool uses_api_key_provider = summary.configured_provider != "bedrock";

  if (stored_auth.has_value()) {
    summary.stored_credential = StoredCredentialLabel(stored_auth->auth);
    if (const auto* oauth =
            std::get_if<provider::OpenAiOAuthAuth>(&stored_auth->auth)) {
      if (oauth->expires_at.has_value()) {
        summary.oauth_expiry = OauthExpiryValue(*oauth->expires_at);
      }
      if (oauth->account_id.has_value() && !oauth->account_id->empty()) {
        summary.account_id = oauth->account_id;
      }
    }
  }

  if (is_openai_provider && config_secrets.env_api_key.has_value()) {
    summary.effective_auth =
        "api (env " + config_secrets.config_result.config.api_key_env + ")";
    if (stored_auth.has_value()) {
      summary.warnings.emplace_back(
          "stored credential is shadowed by " +
          config_secrets.config_result.config.api_key_env);
    }
  } else if (is_openai_provider && stored_auth.has_value()) {
    summary.effective_auth =
        std::holds_alternative<provider::OpenAiApiKeyAuth>(stored_auth->auth)
            ? "api (stored)"
            : "oauth (stored)";
    if (config_secrets.inline_api_key.has_value()) {
      summary.warnings.emplace_back(
          "provider.api_key is shadowed by stored credential");
    }
  } else if (uses_api_key_provider &&
             config_secrets.inline_api_key.has_value()) {
    summary.effective_auth = "api (config)";
  } else if (uses_api_key_provider && config_secrets.env_api_key.has_value()) {
    summary.effective_auth =
        "api (env " + config_secrets.config_result.config.api_key_env + ")";
  }

  if (after_logout) {
    summary.stored_credential.reset();
    summary.oauth_expiry.reset();
    summary.account_id.reset();
    std::optional<std::string> remaining_auth_warning;
    if (is_openai_provider) {
      if (config_secrets.env_api_key.has_value()) {
        remaining_auth_warning =
            config_secrets.config_result.config.api_key_env +
            " remains effective after logout";
      } else if (config_secrets.inline_api_key.has_value()) {
        remaining_auth_warning =
            "provider.api_key remains effective after logout";
      }
    } else if (uses_api_key_provider) {
      if (config_secrets.inline_api_key.has_value()) {
        remaining_auth_warning =
            "provider.api_key remains effective after logout";
      } else if (config_secrets.env_api_key.has_value()) {
        remaining_auth_warning =
            config_secrets.config_result.config.api_key_env +
            " remains effective after logout";
      }
    }
    if (remaining_auth_warning.has_value()) {
      summary.warnings.emplace_back(std::move(*remaining_auth_warning));
    }
  }

  return summary;
}

}  // namespace yac::cli
