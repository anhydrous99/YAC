#include "chat/sqlite_state_store.hpp"
#include "provider/openai_auth_store.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {

class TempDir {
 public:
  explicit TempDir(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
    std::filesystem::create_directories(path_, ec);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  [[nodiscard]] std::filesystem::path DatabasePath() const {
    return path_ / "state.sqlite";
  }

 private:
  std::filesystem::path path_;
};

class MemoryAuthBackend final : public yac::provider::IOpenAiAuthBackend {
 public:
  explicit MemoryAuthBackend(std::optional<std::string> value = std::nullopt)
      : value_(std::move(value)) {}

  [[nodiscard]] std::optional<std::string> Get() const override {
    ++get_count_;
    return value_;
  }

  void Set(std::string_view auth_json) override {
    ++set_count_;
    value_ = std::string(auth_json);
  }

  void Erase() override {
    ++erase_count_;
    value_.reset();
  }

  [[nodiscard]] int GetCount() const { return get_count_; }
  [[nodiscard]] int SetCount() const { return set_count_; }
  [[nodiscard]] int EraseCount() const { return erase_count_; }
  [[nodiscard]] bool HasValue() const { return value_.has_value(); }

 private:
  std::optional<std::string> value_;
  mutable int get_count_ = 0;
  int set_count_ = 0;
  int erase_count_ = 0;
};

class FailingLegacyBackend final : public yac::provider::IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    throw std::runtime_error("legacy backend should not be read");
  }

  void Set(std::string_view auth_json) override {
    (void)auth_json;
    throw std::runtime_error("legacy backend should not be written");
  }

  void Erase() override {
    throw std::runtime_error("legacy backend should not be erased");
  }
};

[[nodiscard]] yac::provider::OpenAiAuthStore::Dependencies Dependencies(
    std::shared_ptr<yac::chat::StateStore> state_store,
    std::shared_ptr<yac::provider::IOpenAiAuthBackend> keychain_backend,
    std::shared_ptr<yac::provider::IOpenAiAuthBackend> file_backend) {
  return yac::provider::OpenAiAuthStore::Dependencies{
      .keychain_backend = std::move(keychain_backend),
      .file_backend = std::move(file_backend),
      .state_store = std::move(state_store),
  };
}

[[nodiscard]] const yac::provider::OpenAiApiKeyAuth* ApiAuth(
    const std::optional<yac::provider::StoredOpenAiAuth>& stored) {
  REQUIRE(stored.has_value());
  const auto* api = std::get_if<yac::provider::OpenAiApiKeyAuth>(&stored->auth);
  REQUIRE(api != nullptr);
  return api;
}

}  // namespace

TEST_CASE("openai auth store saves auth_cli credentials to SQLite first",
          "[openai_auth_sqlite_migration]") {
  TempDir temp_dir("yac_test_openai_auth_sqlite_save");
  auto state_store =
      std::make_shared<yac::chat::SQLiteStateStore>(temp_dir.DatabasePath());
  auto keychain = std::make_shared<MemoryAuthBackend>();
  auto file_backend = std::make_shared<MemoryAuthBackend>();
  yac::provider::OpenAiAuthStore store(
      Dependencies(state_store, keychain, file_backend));

  const auto source =
      store.Save(yac::provider::OpenAiApiKeyAuth{.key = "sk-sqlite-primary"});
  const auto stored = store.Load();
  const auto credential = state_store->LoadProviderCredential(
      yac::ProviderId{"openai"}, std::nullopt,
      yac::chat::StateCredentialType::OpenAiAuth);

  REQUIRE(source == yac::provider::OpenAiAuthStorageSource::StateStore);
  REQUIRE(stored->source == yac::provider::OpenAiAuthStorageSource::StateStore);
  REQUIRE(ApiAuth(stored)->key == "sk-sqlite-primary");
  REQUIRE(credential.has_value());
  REQUIRE(credential->source == yac::chat::StateCredentialSource::AuthCli);
  REQUIRE(keychain->SetCount() == 0);
  REQUIRE(file_backend->SetCount() == 0);
}

TEST_CASE("openai auth store imports legacy file auth once without deleting it",
          "[openai_auth_sqlite_migration]") {
  TempDir temp_dir("yac_test_openai_auth_sqlite_import");
  auto state_store =
      std::make_shared<yac::chat::SQLiteStateStore>(temp_dir.DatabasePath());
  auto keychain = std::make_shared<MemoryAuthBackend>();
  auto file_backend =
      std::make_shared<MemoryAuthBackend>(yac::provider::SerializeOpenAiAuth(
          yac::provider::OpenAiApiKeyAuth{.key = "sk-legacy-file"}));
  yac::provider::OpenAiAuthStore first_store(
      Dependencies(state_store, keychain, file_backend));

  const auto imported = first_store.Load();
  const auto credential = state_store->LoadProviderCredential(
      yac::ProviderId{"openai"}, std::nullopt,
      yac::chat::StateCredentialType::OpenAiAuth);
  yac::provider::OpenAiAuthStore second_store(
      Dependencies(state_store, std::make_shared<FailingLegacyBackend>(),
                   std::make_shared<FailingLegacyBackend>()));
  const auto loaded_again = second_store.Load();

  REQUIRE(imported->source ==
          yac::provider::OpenAiAuthStorageSource::StateStore);
  REQUIRE(ApiAuth(imported)->key == "sk-legacy-file");
  REQUIRE(credential.has_value());
  REQUIRE(credential->source ==
          yac::chat::StateCredentialSource::MigrationLegacyFile);
  REQUIRE(file_backend->HasValue());
  REQUIRE(file_backend->EraseCount() == 0);
  REQUIRE(ApiAuth(loaded_again)->key == "sk-legacy-file");
}

TEST_CASE(
    "openai auth store does not overwrite existing SQLite auth from "
    "legacy",
    "[openai_auth_sqlite_migration]") {
  TempDir temp_dir("yac_test_openai_auth_sqlite_wins");
  auto state_store =
      std::make_shared<yac::chat::SQLiteStateStore>(temp_dir.DatabasePath());
  state_store->SaveProviderCredential(yac::chat::ProviderCredential{
      .provider_id = yac::ProviderId{"openai"},
      .credential_type = yac::chat::StateCredentialType::OpenAiAuth,
      .secret_json = yac::provider::SerializeOpenAiAuth(
          yac::provider::OpenAiApiKeyAuth{.key = "sk-sqlite-newer"}),
      .source = yac::chat::StateCredentialSource::Test,
      .created_at = "1",
      .updated_at = "2",
  });
  auto keychain =
      std::make_shared<MemoryAuthBackend>(yac::provider::SerializeOpenAiAuth(
          yac::provider::OpenAiApiKeyAuth{.key = "sk-legacy-old"}));
  auto file_backend = std::make_shared<MemoryAuthBackend>();
  yac::provider::OpenAiAuthStore store(
      Dependencies(state_store, keychain, file_backend));

  const auto stored = store.Load();

  REQUIRE(stored->source == yac::provider::OpenAiAuthStorageSource::StateStore);
  REQUIRE(ApiAuth(stored)->key == "sk-sqlite-newer");
  REQUIRE(keychain->GetCount() == 0);
  REQUIRE(file_backend->GetCount() == 0);
}

TEST_CASE("openai logout clears SQLite auth without deleting legacy records",
          "[openai_auth_sqlite_migration]") {
  TempDir temp_dir("yac_test_openai_auth_sqlite_logout");
  auto state_store =
      std::make_shared<yac::chat::SQLiteStateStore>(temp_dir.DatabasePath());
  auto file_backend =
      std::make_shared<MemoryAuthBackend>(yac::provider::SerializeOpenAiAuth(
          yac::provider::OpenAiApiKeyAuth{.key = "sk-legacy-after-logout"}));
  yac::provider::OpenAiAuthStore store(Dependencies(
      state_store, std::make_shared<MemoryAuthBackend>(), file_backend));
  static_cast<void>(store.Save(
      yac::provider::OpenAiApiKeyAuth{.key = "sk-sqlite-before-logout"}));

  store.Erase();
  yac::provider::OpenAiAuthStore after_logout(Dependencies(
      state_store, std::make_shared<FailingLegacyBackend>(), file_backend));

  REQUIRE_FALSE(after_logout.Load().has_value());
  REQUIRE_FALSE(
      state_store
          ->LoadProviderCredential(yac::ProviderId{"openai"}, std::nullopt,
                                   yac::chat::StateCredentialType::OpenAiAuth)
          .has_value());
  REQUIRE(file_backend->HasValue());
  REQUIRE(file_backend->EraseCount() == 0);
}
