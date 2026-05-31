#pragma once

#include "chat/state_store.hpp"

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

namespace yac::testing {

class FakeStateStore final : public yac::chat::StateStore {
 public:
  void SaveProviderProfile(const yac::chat::ProviderProfile& profile) override {
    profiles_[profile.profile_id] = profile;
  }

  [[nodiscard]] std::optional<yac::chat::ProviderProfile> LoadProviderProfile(
      std::string_view profile_id) const override {
    const auto it = profiles_.find(std::string(profile_id));
    if (it == profiles_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  [[nodiscard]] std::vector<yac::chat::ProviderProfile> ListProviderProfiles()
      const override {
    std::vector<yac::chat::ProviderProfile> profiles;
    profiles.reserve(profiles_.size());
    for (const auto& [profile_id, profile] : profiles_) {
      profiles.push_back(profile);
    }
    return profiles;
  }

  void DeleteProviderProfile(std::string_view profile_id) override {
    profiles_.erase(std::string(profile_id));
  }

  void SaveProviderCredential(
      const yac::chat::ProviderCredential& credential) override {
    credentials_[CredentialKey(credential.provider_id, credential.profile_id,
                               credential.credential_type)] = credential;
  }

  [[nodiscard]] std::optional<yac::chat::ProviderCredential>
  LoadProviderCredential(
      const yac::ProviderId& provider_id,
      std::optional<std::string_view> profile_id,
      yac::chat::StateCredentialType credential_type) const override {
    const auto it = credentials_.find(CredentialKey(
        provider_id, OptionalString(profile_id), credential_type));
    if (it == credentials_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  [[nodiscard]] std::vector<yac::chat::ProviderCredential>
  ListProviderCredentials() const override {
    std::vector<yac::chat::ProviderCredential> credentials;
    credentials.reserve(credentials_.size());
    for (const auto& [key, credential] : credentials_) {
      credentials.push_back(credential);
    }
    return credentials;
  }

  void DeleteProviderCredential(
      const yac::ProviderId& provider_id,
      std::optional<std::string_view> profile_id,
      yac::chat::StateCredentialType credential_type) override {
    credentials_.erase(CredentialKey(provider_id, OptionalString(profile_id),
                                     credential_type));
  }

  void SaveAppState(const yac::chat::AppStateEntry& entry) override {
    app_state_[entry.key] = entry;
  }

  [[nodiscard]] std::optional<yac::chat::AppStateEntry> LoadAppState(
      std::string_view key) const override {
    const auto it = app_state_.find(std::string(key));
    if (it == app_state_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  [[nodiscard]] std::vector<yac::chat::AppStateEntry> ListAppState()
      const override {
    std::vector<yac::chat::AppStateEntry> entries;
    entries.reserve(app_state_.size());
    for (const auto& [key, entry] : app_state_) {
      entries.push_back(entry);
    }
    return entries;
  }

  void DeleteAppState(std::string_view key) override {
    app_state_.erase(std::string(key));
  }

 private:
  using CredentialKeyType =
      std::tuple<std::string, std::string, yac::chat::StateCredentialType>;

  [[nodiscard]] static std::optional<std::string> OptionalString(
      std::optional<std::string_view> value) {
    if (!value.has_value()) {
      return std::nullopt;
    }
    return std::string(*value);
  }

  [[nodiscard]] static CredentialKeyType CredentialKey(
      const yac::ProviderId& provider_id,
      const std::optional<std::string>& profile_id,
      yac::chat::StateCredentialType credential_type) {
    return {provider_id.value, profile_id.value_or(std::string{}),
            credential_type};
  }

  std::map<std::string, yac::chat::ProviderProfile> profiles_;
  std::map<CredentialKeyType, yac::chat::ProviderCredential> credentials_;
  std::map<std::string, yac::chat::AppStateEntry> app_state_;
};

}  // namespace yac::testing
