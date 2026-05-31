#pragma once

#include "core_types/typed_ids.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace yac::chat {

using ::yac::ModelId;
using ::yac::ProviderId;

enum class StateCredentialType { ApiKey, OpenAiAuth };

enum class StateCredentialSource {
  AuthCli,
  MigrationLegacyFile,
  MigrationKeychain,
  ManualCli,
  Test,
};

inline constexpr std::string_view kAppStateLastProviderId = "last_provider_id";
inline constexpr std::string_view kAppStateLastProfileId = "last_profile_id";
inline constexpr std::string_view kAppStateLastModel = "last_model";

struct ProviderProfile {
  std::string profile_id;
  ProviderId provider_id;
  std::string display_name;
  std::optional<std::string> base_url;
  std::optional<ModelId> default_model;
  std::string options_json = "{}";
  bool enabled = true;
  std::string created_at;
  std::string updated_at;

  friend bool operator==(const ProviderProfile&,
                         const ProviderProfile&) = default;
};

struct ProviderCredential {
  ProviderId provider_id;
  std::optional<std::string> profile_id;
  StateCredentialType credential_type = StateCredentialType::ApiKey;
  std::string secret_json;
  StateCredentialSource source = StateCredentialSource::ManualCli;
  std::string created_at;
  std::string updated_at;

  friend bool operator==(const ProviderCredential&,
                         const ProviderCredential&) = default;
};

struct AppStateEntry {
  std::string key;
  std::string value;
  std::string updated_at;

  friend bool operator==(const AppStateEntry&, const AppStateEntry&) = default;
};

[[nodiscard]] std::string_view ToString(StateCredentialType type);
[[nodiscard]] std::optional<StateCredentialType> CredentialTypeFromString(
    std::string_view value);
[[nodiscard]] std::string_view ToString(StateCredentialSource source);
[[nodiscard]] std::optional<StateCredentialSource> CredentialSourceFromString(
    std::string_view value);
[[nodiscard]] std::string LastModelEffortAppStateKey(
    const ProviderId& provider_id, const ModelId& model);

class StateStore {
 public:
  StateStore() = default;
  virtual ~StateStore() = default;
  StateStore(const StateStore&) = delete;
  StateStore& operator=(const StateStore&) = delete;
  StateStore(StateStore&&) = delete;
  StateStore& operator=(StateStore&&) = delete;

  virtual void SaveProviderProfile(const ProviderProfile& profile) = 0;
  [[nodiscard]] virtual std::optional<ProviderProfile> LoadProviderProfile(
      std::string_view profile_id) const = 0;
  [[nodiscard]] virtual std::vector<ProviderProfile> ListProviderProfiles()
      const = 0;
  virtual void DeleteProviderProfile(std::string_view profile_id) = 0;

  virtual void SaveProviderCredential(const ProviderCredential& credential) = 0;
  [[nodiscard]] virtual std::optional<ProviderCredential>
  LoadProviderCredential(const ProviderId& provider_id,
                         std::optional<std::string_view> profile_id,
                         StateCredentialType credential_type) const = 0;
  [[nodiscard]] virtual std::vector<ProviderCredential>
  ListProviderCredentials() const = 0;
  virtual void DeleteProviderCredential(
      const ProviderId& provider_id, std::optional<std::string_view> profile_id,
      StateCredentialType credential_type) = 0;

  virtual void SaveAppState(const AppStateEntry& entry) = 0;
  [[nodiscard]] virtual std::optional<AppStateEntry> LoadAppState(
      std::string_view key) const = 0;
  [[nodiscard]] virtual std::vector<AppStateEntry> ListAppState() const = 0;
  virtual void DeleteAppState(std::string_view key) = 0;
};

}  // namespace yac::chat
