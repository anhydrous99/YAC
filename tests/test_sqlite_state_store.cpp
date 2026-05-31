#include "chat/sqlite_state_store.hpp"
#include "chat/state_paths.hpp"
#include "chat/state_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using Catch::Matchers::ContainsSubstring;
using yac::chat::AppStateEntry;
using yac::chat::ProviderCredential;
using yac::chat::ProviderProfile;
using yac::chat::SQLiteStateStore;
using yac::chat::StateCredentialSource;
using yac::chat::StateCredentialType;
using yac::chat::StateDatabasePath;
using yac::chat::StateStore;

namespace {

class TempDir {
 public:
  explicit TempDir(std::string_view prefix) {
#ifndef _WIN32
    std::string tmpl = (std::filesystem::temp_directory_path() /
                        (std::string(prefix) + "_XXXXXX"))
                           .string();
    const char* result = ::mkdtemp(tmpl.data());
    if (result == nullptr) {
      throw std::runtime_error("mkdtemp failed");
    }
    path_ = result;
#else
    path_ = std::filesystem::temp_directory_path() / std::string(prefix);
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
#endif
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

class ScopedHome {
 public:
  explicit ScopedHome(const std::filesystem::path& value) {
    if (const char* prior = std::getenv("HOME")) {
      had_prior_ = true;
      prior_ = prior;
    }
    ::setenv("HOME", value.c_str(), 1);
  }

  ~ScopedHome() {
    if (had_prior_) {
      ::setenv("HOME", prior_.c_str(), 1);
    } else {
      ::unsetenv("HOME");
    }
  }
  ScopedHome(const ScopedHome&) = delete;
  ScopedHome& operator=(const ScopedHome&) = delete;
  ScopedHome(ScopedHome&&) = delete;
  ScopedHome& operator=(ScopedHome&&) = delete;

 private:
  bool had_prior_ = false;
  std::string prior_;
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

std::string ScalarText(sqlite3* db, std::string_view sql) {
  sqlite3_stmt* stmt = nullptr;
  REQUIRE(sqlite3_prepare_v2(db, std::string(sql).c_str(), -1, &stmt,
                             nullptr) == SQLITE_OK);
  REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
  const unsigned char* text = sqlite3_column_text(stmt, 0);
  std::string value =
      text == nullptr ? "" : reinterpret_cast<const char*>(text);
  REQUIRE(sqlite3_finalize(stmt) == SQLITE_OK);
  return value;
}

int ScalarInt(sqlite3* db, std::string_view sql) {
  sqlite3_stmt* stmt = nullptr;
  REQUIRE(sqlite3_prepare_v2(db, std::string(sql).c_str(), -1, &stmt,
                             nullptr) == SQLITE_OK);
  REQUIRE(sqlite3_step(stmt) == SQLITE_ROW);
  const int value = sqlite3_column_int(stmt, 0);
  REQUIRE(sqlite3_finalize(stmt) == SQLITE_OK);
  return value;
}

std::vector<std::string> TableColumns(sqlite3* db, std::string_view table) {
  sqlite3_stmt* stmt = nullptr;
  const std::string sql = "PRAGMA table_info(" + std::string(table) + ")";
  REQUIRE(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK);
  std::vector<std::string> columns;
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    columns.emplace_back(
        reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1)));
  }
  REQUIRE(sqlite3_finalize(stmt) == SQLITE_OK);
  return columns;
}

ProviderProfile PrimaryProfile() {
  return ProviderProfile{.profile_id = "primary",
                         .provider_id = yac::ProviderId{"openai"},
                         .display_name = "OpenAI Primary",
                         .base_url = "https://api.openai.test/v1",
                         .default_model = yac::ModelId{"gpt-test"},
                         .options_json = R"({"temperature":0.2})",
                         .enabled = true,
                         .created_at = "2026-05-30T00:00:00Z",
                         .updated_at = "2026-05-30T00:01:00Z"};
}

ProviderCredential ProviderAuthCredential() {
  return ProviderCredential{.provider_id = yac::ProviderId{"openai"},
                            .profile_id = std::nullopt,
                            .credential_type = StateCredentialType::OpenAiAuth,
                            .secret_json = R"({"refresh":"token"})",
                            .source = StateCredentialSource::AuthCli,
                            .created_at = "2026-05-30T00:00:00Z",
                            .updated_at = "2026-05-30T00:02:00Z"};
}

#ifndef _WIN32
mode_t ModeOf(const std::filesystem::path& path) {
  struct ::stat st{};
  REQUIRE(::stat(path.c_str(), &st) == 0);
  return st.st_mode & 0777;
}
#endif

}  // namespace

TEST_CASE("StateDatabasePath resolves under ~/.yac") {
  TempDir temp("yac_sqlite_path");
  ScopedHome home(temp.Path());

  REQUIRE(StateDatabasePath() == temp.Path() / ".yac" / "state.sqlite");
  REQUIRE(StateDatabasePath(temp.Path()) ==
          temp.Path() / ".yac" / "state.sqlite");
}

TEST_CASE("FreshDatabase creates schema version one with private permissions") {
  TempDir temp("yac_sqlite_fresh");
  ScopedHome home(temp.Path());

  SQLiteStateStore store(StateDatabasePath());

  const auto db_path = temp.Path() / ".yac" / "state.sqlite";
  REQUIRE(std::filesystem::exists(db_path));
  SqliteDb db(db_path);
  REQUIRE(ScalarInt(db.Get(), "PRAGMA user_version") == 1);
  REQUIRE(ScalarText(db.Get(), "PRAGMA journal_mode") == "wal");
  REQUIRE(TableColumns(db.Get(), "provider_profiles") ==
          std::vector<std::string>{"profile_id", "provider_id", "display_name",
                                   "base_url", "default_model", "options_json",
                                   "enabled", "created_at", "updated_at"});
  REQUIRE(TableColumns(db.Get(), "provider_credentials") ==
          std::vector<std::string>{"provider_id", "profile_id",
                                   "credential_type", "secret_json", "source",
                                   "created_at", "updated_at"});
  REQUIRE(TableColumns(db.Get(), "app_state") ==
          std::vector<std::string>{"key", "value", "updated_at"});

#ifndef _WIN32
  REQUIRE(ModeOf(temp.Path() / ".yac") == 0700);
  REQUIRE(ModeOf(db_path) == 0600);
  for (const auto& sidecar :
       {db_path.string() + "-wal", db_path.string() + "-shm"}) {
    if (std::filesystem::exists(sidecar)) {
      REQUIRE(ModeOf(sidecar) == 0600);
    }
  }
#endif
}

TEST_CASE("SQLiteStateStore round trips profiles credentials and app state") {
  TempDir temp("yac_sqlite_round_trip");
  const auto db_path = temp.Path() / ".yac" / "state.sqlite";
  const ProviderProfile profile = PrimaryProfile();
  const ProviderCredential credential = ProviderAuthCredential();
  const AppStateEntry app_state{.key = "last_model",
                                .value = "gpt-test",
                                .updated_at = "2026-05-30T00:03:00Z"};

  {
    SQLiteStateStore store(db_path);
    StateStore& state = store;
    state.SaveProviderProfile(profile);
    state.SaveProviderCredential(credential);
    state.SaveAppState(app_state);
  }

  SQLiteStateStore reopened(db_path);
  StateStore& state = reopened;
  REQUIRE(state.LoadProviderProfile("primary") == profile);
  REQUIRE(state.LoadProviderCredential(yac::ProviderId{"openai"}, std::nullopt,
                                       StateCredentialType::OpenAiAuth) ==
          credential);
  REQUIRE(state.LoadAppState("last_model") == app_state);
  REQUIRE(state.ListProviderProfiles() ==
          std::vector<ProviderProfile>{profile});
  REQUIRE(state.ListProviderCredentials() ==
          std::vector<ProviderCredential>{credential});
  REQUIRE(state.ListAppState() == std::vector<AppStateEntry>{app_state});

  state.DeleteProviderProfile("primary");
  state.DeleteProviderCredential(yac::ProviderId{"openai"}, std::nullopt,
                                 StateCredentialType::OpenAiAuth);
  state.DeleteAppState("last_model");
  REQUIRE_FALSE(state.LoadProviderProfile("primary").has_value());
  REQUIRE_FALSE(state
                    .LoadProviderCredential(yac::ProviderId{"openai"},
                                            std::nullopt,
                                            StateCredentialType::OpenAiAuth)
                    .has_value());
  REQUIRE_FALSE(state.LoadAppState("last_model").has_value());
}

TEST_CASE("MigrationFromVersionZero is transactional and idempotent") {
  TempDir temp("yac_sqlite_migrate");
  const auto db_path = temp.Path() / ".yac" / "state.sqlite";
  std::filesystem::create_directories(db_path.parent_path());
  {
    SqliteDb db(db_path);
    Exec(db.Get(), "PRAGMA user_version=0");
  }

  SQLiteStateStore first_open(db_path);
  first_open.SaveAppState({.key = "last_provider_id",
                           .value = "openai",
                           .updated_at = "2026-05-30T00:00:00Z"});
  SQLiteStateStore second_open(db_path);

  REQUIRE(second_open.LoadAppState("last_provider_id")->value == "openai");
  SqliteDb db(db_path);
  REQUIRE(ScalarInt(db.Get(), "PRAGMA user_version") == 1);
  REQUIRE(ScalarInt(db.Get(),
                    "SELECT count(*) FROM sqlite_master WHERE type='table' "
                    "AND name IN ('provider_profiles', "
                    "'provider_credentials', 'app_state')") == 3);
}

TEST_CASE("Permissions are repaired for existing state directory and file") {
  TempDir temp("yac_sqlite_permissions");
  const auto yac_dir = temp.Path() / ".yac";
  const auto db_path = yac_dir / "state.sqlite";
  std::filesystem::create_directories(yac_dir);
  {
    SqliteDb db(db_path);
    Exec(db.Get(), "PRAGMA user_version=0");
  }
#ifndef _WIN32
  ::chmod(yac_dir.c_str(), 0755);
  ::chmod(db_path.c_str(), 0644);
#endif

  SQLiteStateStore store(db_path);

#ifndef _WIN32
  REQUIRE(ModeOf(yac_dir) == 0700);
  REQUIRE(ModeOf(db_path) == 0600);
#else
  REQUIRE(std::filesystem::exists(db_path));
#endif
}

TEST_CASE("Values are bound with prepared statements") {
  TempDir temp("yac_sqlite_prepared");
  SQLiteStateStore store(temp.Path() / ".yac" / "state.sqlite");
  ProviderProfile profile = PrimaryProfile();
  profile.profile_id = "quoted'); DROP TABLE app_state; --";

  store.SaveProviderProfile(profile);
  store.SaveAppState({.key = "safe-key",
                      .value = "safe-value",
                      .updated_at = "2026-05-30T00:00:00Z"});

  REQUIRE(store.LoadProviderProfile(profile.profile_id) == profile);
  REQUIRE(store.LoadAppState("safe-key")->value == "safe-value");
}

TEST_CASE("InvalidCredentialSource fails with actionable store error") {
  TempDir temp("yac_sqlite_invalid_source");
  const auto db_path = temp.Path() / ".yac" / "state.sqlite";
  SQLiteStateStore store(db_path);
  {
    SqliteDb db(db_path);
    Exec(db.Get(),
         "INSERT INTO provider_credentials(provider_id, profile_id, "
         "credential_type, secret_json, source, created_at, updated_at) "
         "VALUES('openai', NULL, 'api_key', '{}', 'unknown', "
         "'2026-05-30T00:00:00Z', '2026-05-30T00:00:00Z')");
  }

  REQUIRE_THROWS_WITH(store.ListProviderCredentials(),
                      ContainsSubstring("unknown credential source"));
}

TEST_CASE("CorruptDatabase fails clearly") {
  TempDir temp("yac_sqlite_corrupt");
  const auto db_path = temp.Path() / ".yac" / "state.sqlite";
  std::filesystem::create_directories(db_path.parent_path());
  std::ofstream out(db_path, std::ios::binary | std::ios::trunc);
  out << "not a sqlite database";
  out.close();

  REQUIRE_THROWS_WITH(SQLiteStateStore(db_path),
                      ContainsSubstring("SQLiteStateStore"));
  REQUIRE_THROWS_WITH(SQLiteStateStore(db_path), ContainsSubstring("corrupt"));
}
