#include "chat/state_store.hpp"

#include <string>

namespace yac::chat {

std::string_view ToString(StateCredentialType type) {
  switch (type) {
    case StateCredentialType::ApiKey:
      return "api_key";
    case StateCredentialType::OpenAiAuth:
      return "openai_auth";
  }
  return "";
}

std::optional<StateCredentialType> CredentialTypeFromString(
    std::string_view value) {
  if (value == "api_key") {
    return StateCredentialType::ApiKey;
  }
  if (value == "openai_auth") {
    return StateCredentialType::OpenAiAuth;
  }
  return std::nullopt;
}

std::string_view ToString(StateCredentialSource source) {
  switch (source) {
    case StateCredentialSource::AuthCli:
      return "auth_cli";
    case StateCredentialSource::MigrationLegacyFile:
      return "migration_legacy_file";
    case StateCredentialSource::MigrationKeychain:
      return "migration_keychain";
    case StateCredentialSource::ManualCli:
      return "manual_cli";
    case StateCredentialSource::Test:
      return "test";
  }
  return "";
}

std::optional<StateCredentialSource> CredentialSourceFromString(
    std::string_view value) {
  if (value == "auth_cli") {
    return StateCredentialSource::AuthCli;
  }
  if (value == "migration_legacy_file") {
    return StateCredentialSource::MigrationLegacyFile;
  }
  if (value == "migration_keychain") {
    return StateCredentialSource::MigrationKeychain;
  }
  if (value == "manual_cli") {
    return StateCredentialSource::ManualCli;
  }
  if (value == "test") {
    return StateCredentialSource::Test;
  }
  return std::nullopt;
}

std::string LastModelEffortAppStateKey(const ProviderId& provider_id,
                                       const ModelId& model) {
  return "last_model_effort:" + provider_id.value + ":" + model.value;
}

}  // namespace yac::chat
