#pragma once

#include "chat/state_store.hpp"

#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <vector>

struct sqlite3;

namespace yac::chat {

class SQLiteStateStoreError : public std::runtime_error {
 public:
  using std::runtime_error::runtime_error;
};

class SQLiteStateStore final : public StateStore {
 public:
  explicit SQLiteStateStore(std::filesystem::path database_path);
  SQLiteStateStore();
  ~SQLiteStateStore() override;

  SQLiteStateStore(const SQLiteStateStore&) = delete;
  SQLiteStateStore& operator=(const SQLiteStateStore&) = delete;
  SQLiteStateStore(SQLiteStateStore&&) = delete;
  SQLiteStateStore& operator=(SQLiteStateStore&&) = delete;

  void SaveProviderProfile(const ProviderProfile& profile) override;
  [[nodiscard]] std::optional<ProviderProfile> LoadProviderProfile(
      std::string_view profile_id) const override;
  [[nodiscard]] std::vector<ProviderProfile> ListProviderProfiles()
      const override;
  void DeleteProviderProfile(std::string_view profile_id) override;

  void SaveProviderCredential(const ProviderCredential& credential) override;
  [[nodiscard]] std::optional<ProviderCredential> LoadProviderCredential(
      const ProviderId& provider_id, std::optional<std::string_view> profile_id,
      StateCredentialType credential_type) const override;
  [[nodiscard]] std::vector<ProviderCredential> ListProviderCredentials()
      const override;
  void DeleteProviderCredential(const ProviderId& provider_id,
                                std::optional<std::string_view> profile_id,
                                StateCredentialType credential_type) override;

  void SaveAppState(const AppStateEntry& entry) override;
  [[nodiscard]] std::optional<AppStateEntry> LoadAppState(
      std::string_view key) const override;
  [[nodiscard]] std::vector<AppStateEntry> ListAppState() const override;
  void DeleteAppState(std::string_view key) override;

 private:
  sqlite3* db_ = nullptr;
  std::filesystem::path database_path_;
};

}  // namespace yac::chat
