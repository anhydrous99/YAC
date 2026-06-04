#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/sqlite_state_store.hpp"
#include "chat/state_store.hpp"
#include "cli/provider_auth_command.hpp"
#include "config_env_test_helpers.hpp"
#include "provider/openai_auth_store.hpp"

#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::ContainsSubstring;
using yac::chat::AppStateEntry;
using yac::chat::ChatConfig;
using yac::chat::ChatService;
using yac::chat::ConfigIssueSeverity;
using yac::chat::ConfigValueSource;
using yac::chat::LoadChatConfigResultFrom;
using yac::chat::ProviderCredential;
using yac::chat::ProviderProfile;
using yac::chat::ReasoningEffort;
using yac::chat::SQLiteStateStore;
using yac::chat::StateCredentialSource;
using yac::chat::StateCredentialType;
using yac::testing::ScopedEnvClear;

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

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class SqliteDb {
 public:
  explicit SqliteDb(const std::filesystem::path& path) {
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK) {
      const std::string message = sqlite3_errmsg(db_);
      sqlite3_close(db_);
      db_ = nullptr;
      throw std::runtime_error(message);
    }
  }

  ~SqliteDb() {
    if (db_ != nullptr) {
      sqlite3_close(db_);
    }
  }
  SqliteDb(const SqliteDb&) = delete;
  SqliteDb& operator=(const SqliteDb&) = delete;
  SqliteDb(SqliteDb&&) = delete;
  SqliteDb& operator=(SqliteDb&&) = delete;

  [[nodiscard]] sqlite3* Get() const { return db_; }

 private:
  sqlite3* db_ = nullptr;
};

class MemoryAuthBackend final : public yac::provider::IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    return std::nullopt;
  }
  void Set(std::string_view auth_json) override { (void)auth_json; }
  void Erase() override {}
};

class PartialFailingCredentialStore final : public yac::chat::StateStore {
 public:
  PartialFailingCredentialStore() = default;
  explicit PartialFailingCredentialStore(std::string existing_secret_json)
      : partial_secret_json_(std::move(existing_secret_json)) {}

  void SaveProviderProfile(const ProviderProfile& profile) override {
    (void)profile;
  }
  [[nodiscard]] std::optional<ProviderProfile> LoadProviderProfile(
      std::string_view profile_id) const override {
    (void)profile_id;
    return std::nullopt;
  }
  [[nodiscard]] std::vector<ProviderProfile> ListProviderProfiles()
      const override {
    return {};
  }
  void DeleteProviderProfile(std::string_view profile_id) override {
    (void)profile_id;
  }

  void SaveProviderCredential(const ProviderCredential& credential) override {
    partial_secret_json_ = credential.secret_json;
    throw std::runtime_error("simulated credential write failure");
  }
  [[nodiscard]] std::optional<ProviderCredential> LoadProviderCredential(
      const yac::ProviderId& provider_id,
      std::optional<std::string_view> profile_id,
      StateCredentialType credential_type) const override {
    (void)provider_id;
    (void)profile_id;
    (void)credential_type;
    if (!partial_secret_json_.has_value()) {
      return std::nullopt;
    }
    return ProviderCredential{
        .provider_id = yac::ProviderId{"openai"},
        .credential_type = StateCredentialType::OpenAiAuth,
        .secret_json = *partial_secret_json_,
        .source = StateCredentialSource::Test,
        .created_at = "1",
        .updated_at = "1"};
  }
  [[nodiscard]] std::vector<ProviderCredential> ListProviderCredentials()
      const override {
    return {};
  }
  void DeleteProviderCredential(const yac::ProviderId& provider_id,
                                std::optional<std::string_view> profile_id,
                                StateCredentialType credential_type) override {
    (void)provider_id;
    (void)profile_id;
    (void)credential_type;
  }

  void SaveAppState(const AppStateEntry& entry) override { (void)entry; }
  [[nodiscard]] std::optional<AppStateEntry> LoadAppState(
      std::string_view key) const override {
    (void)key;
    return std::nullopt;
  }
  [[nodiscard]] std::vector<AppStateEntry> ListAppState() const override {
    return {};
  }
  void DeleteAppState(std::string_view key) override { (void)key; }

 private:
  mutable std::optional<std::string> partial_secret_json_;
};

class FailingAppStateStore final : public yac::chat::StateStore {
 public:
  void SaveProviderProfile(const ProviderProfile& profile) override {
    (void)profile;
  }
  [[nodiscard]] std::optional<ProviderProfile> LoadProviderProfile(
      std::string_view profile_id) const override {
    (void)profile_id;
    return std::nullopt;
  }
  [[nodiscard]] std::vector<ProviderProfile> ListProviderProfiles()
      const override {
    return {};
  }
  void DeleteProviderProfile(std::string_view profile_id) override {
    (void)profile_id;
  }
  void SaveProviderCredential(const ProviderCredential& credential) override {
    (void)credential;
  }
  [[nodiscard]] std::optional<ProviderCredential> LoadProviderCredential(
      const yac::ProviderId& provider_id,
      std::optional<std::string_view> profile_id,
      StateCredentialType credential_type) const override {
    (void)provider_id;
    (void)profile_id;
    (void)credential_type;
    return std::nullopt;
  }
  [[nodiscard]] std::vector<ProviderCredential> ListProviderCredentials()
      const override {
    return {};
  }
  void DeleteProviderCredential(const yac::ProviderId& provider_id,
                                std::optional<std::string_view> profile_id,
                                StateCredentialType credential_type) override {
    (void)provider_id;
    (void)profile_id;
    (void)credential_type;
    throw std::runtime_error("simulated app_state delete failure");
  }
  void SaveAppState(const AppStateEntry& entry) override {
    (void)entry;
    throw std::runtime_error("simulated app_state write failure");
  }
  [[nodiscard]] std::optional<AppStateEntry> LoadAppState(
      std::string_view key) const override {
    (void)key;
    return std::nullopt;
  }
  [[nodiscard]] std::vector<AppStateEntry> ListAppState() const override {
    return {};
  }
  void DeleteAppState(std::string_view key) override {
    (void)key;
    throw std::runtime_error("simulated app_state delete failure");
  }
};

void Exec(sqlite3* db, std::string_view sql) {
  char* error = nullptr;
  const int rc =
      sqlite3_exec(db, std::string(sql).c_str(), nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    const std::string message = error == nullptr ? sqlite3_errmsg(db) : error;
    sqlite3_free(error);
    throw std::runtime_error(message);
  }
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << content;
}

ChatConfig RuntimeConfig(std::shared_ptr<yac::chat::StateStore> store) {
  ChatConfig config;
  config.provider_id = yac::ProviderId{"openai-compatible"};
  config.model = yac::ModelId{"gpt-4o"};
  config.state_store = std::move(store);
  return config;
}

}  // namespace

TEST_CASE("MissingDatabase creates lazily") {
  TempDir temp("yac_state_failures_missing");
  const auto database_path = temp.Path() / ".yac" / "state.sqlite";

  SQLiteStateStore store(database_path);

  REQUIRE(std::filesystem::exists(database_path));
  REQUIRE_FALSE(store.LoadAppState(yac::chat::kAppStateLastModel).has_value());
}

TEST_CASE("CorruptDatabase reports actionable redacted error") {
  TempDir temp("yac_state_failures_corrupt");
  const auto database_path = temp.Path() / ".yac" / "state.sqlite";
  WriteFile(database_path,
            "not sqlite sk-secret access-token refresh-token provider secret");

  try {
    SQLiteStateStore store(database_path);
    FAIL("expected corrupt database open to throw");
  } catch (const std::exception& error) {
    const std::string message = error.what();
    CHECK_THAT(message, ContainsSubstring("SQLite state database"));
    CHECK_THAT(message, ContainsSubstring("move"));
    CHECK(message.find("sk-secret") == std::string::npos);
    CHECK(message.find("access-token") == std::string::npos);
    CHECK(message.find("refresh-token") == std::string::npos);
    CHECK(message.find("provider secret") == std::string::npos);
  }
}

TEST_CASE("Invalid app_state and profile rows fall back without crashing") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir temp("yac_state_failures_invalid_config_state");
  const auto settings_path = temp.Path() / "settings.toml";
  const auto database_path = temp.Path() / "state.sqlite";
  WriteFile(settings_path, "");
  {
    SQLiteStateStore store(database_path);
    store.SaveAppState({.key = std::string(yac::chat::kAppStateLastProfileId),
                        .value = "bad-profile",
                        .updated_at = "2026-05-30T00:00:00Z"});
  }
  {
    SqliteDb db(database_path);
    Exec(db.Get(),
         "INSERT INTO provider_profiles(profile_id, provider_id, display_name, "
         "base_url, default_model, options_json, enabled, created_at, "
         "updated_at) VALUES('bad-profile', 'openai', 'Bad', "
         "'https://bad.example.invalid/v1', 'bad-model', '{not json', 1, "
         "'2026-05-30T00:00:00Z', '2026-05-30T00:00:00Z')");
  }
  SQLiteStateStore store(database_path);

  const auto loaded = LoadChatConfigResultFrom(settings_path, false, &store);

  REQUIRE(loaded.config.provider_id.value == "openai-compatible");
  REQUIRE_FALSE(loaded.config.profile_id.has_value());
  REQUIRE(loaded.config.model.value == "gpt-4o-mini");
  REQUIRE(loaded.config.base_url == "https://api.openai.com/v1/");
  REQUIRE(loaded.config.source.provider_id ==
          ConfigValueSource::BuiltInDefault);
  REQUIRE(loaded.config.source.model == ConfigValueSource::BuiltInDefault);
  REQUIRE(loaded.config.source.base_url == ConfigValueSource::BuiltInDefault);
  REQUIRE_FALSE(loaded.issues.empty());
  CHECK(loaded.issues.front().severity == ConfigIssueSeverity::Warning);
}

TEST_CASE("Credential write failure remains fatal to auth store") {
  auto state_store = std::make_shared<PartialFailingCredentialStore>();
  auto backend = std::make_shared<MemoryAuthBackend>();
  yac::provider::OpenAiAuthStore store(
      yac::provider::OpenAiAuthStore::Dependencies{.keychain_backend = backend,
                                                   .file_backend = backend,
                                                   .state_store = state_store});

  REQUIRE_THROWS_WITH(
      store.Save(yac::provider::OpenAiApiKeyAuth{.key = "sk-fatal-write"}),
      ContainsSubstring("credential write failure"));
}

TEST_CASE("Login command fails when replacing an existing credential fails") {
  auto state_store = std::make_shared<PartialFailingCredentialStore>(
      yac::provider::SerializeOpenAiAuth(
          yac::provider::OpenAiApiKeyAuth{.key = "sk-existing"}));
  auto backend = std::make_shared<MemoryAuthBackend>();
  auto auth_store = std::make_shared<yac::provider::OpenAiAuthStore>(
      yac::provider::OpenAiAuthStore::Dependencies{.keychain_backend = backend,
                                                   .file_backend = backend,
                                                   .state_store = state_store});
  yac::cli::ProviderAuthCommand command({
      .auth_store = auth_store,
      .login_fn =
          [](const yac::provider::OpenAiAuthorizationObserver&) {
            return yac::provider::OpenAiOAuthAuth{
                .refresh_token = "refresh-new", .access_token = "access-new"};
          },
  });

  REQUIRE_THROWS_WITH(command.LoginOpenAi(),
                      ContainsSubstring("credential write failure"));
}

TEST_CASE(
    "Runtime app_state write and delete failures do not abort mutations") {
  auto state_store = std::make_shared<FailingAppStateStore>();
  ChatService service({}, RuntimeConfig(state_store));

  REQUIRE_NOTHROW(service.SetProvider(yac::ProviderId{"openai"}));
  REQUIRE_NOTHROW(service.SetModel(yac::ModelId{"gpt-5.5"}));
  REQUIRE_NOTHROW(service.SetReasoningEffort(ReasoningEffort::Medium));
  REQUIRE_NOTHROW(service.SetReasoningEffort(std::nullopt));

  const auto snapshot = service.ConfigSnapshot();
  REQUIRE(snapshot.provider_id.value == "openai");
  REQUIRE(snapshot.model.value == "gpt-5.5");
  REQUIRE(snapshot.model_settings.size() == 1);
  REQUIRE_FALSE(snapshot.model_settings[0].effort.has_value());
}
