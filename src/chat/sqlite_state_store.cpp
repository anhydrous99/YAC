#include "chat/sqlite_state_store.hpp"

#include "chat/state_paths.hpp"

#include <cerrno>
#include <cstring>
#include <filesystem>
#include <optional>
#include <sqlite3.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace yac::chat {
namespace {

constexpr int kSchemaVersion = 1;
constexpr auto kDirectoryOwnerOnly = std::filesystem::perms::owner_all;
constexpr auto kFileOwnerOnly =
    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;

[[nodiscard]] std::string SqliteMessage(sqlite3* db) {
  if (db == nullptr) {
    return "unknown SQLite error";
  }
  std::string message = sqlite3_errmsg(db);
  if (message.find("file is not a database") != std::string::npos ||
      message.find("malformed") != std::string::npos) {
    return "SQLite state database appears corrupt. move ~/.yac/state.sqlite "
           "aside and restart YAC to recreate it.";
  }
  if (message.find("database is locked") != std::string::npos ||
      message.find("database table is locked") != std::string::npos) {
    return "SQLite state database is locked after waiting up to 5000ms. Close "
           "other YAC processes or inspect ~/.yac/state.sqlite.";
  }
  return message;
}

[[noreturn]] void ThrowSqlite(sqlite3* db, std::string_view action) {
  throw SQLiteStateStoreError("SQLiteStateStore: " + std::string(action) +
                              ": " + SqliteMessage(db));
}

void EnsureOk(sqlite3* db, int rc, std::string_view action) {
  if (rc != SQLITE_OK) {
    ThrowSqlite(db, action);
  }
}

void EnsureDone(sqlite3* db, int rc, std::string_view action) {
  if (rc != SQLITE_DONE) {
    ThrowSqlite(db, action);
  }
}

void EnsurePrivateDirectory(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    throw SQLiteStateStoreError("SQLiteStateStore: cannot create directory " +
                                path.string() + ": " + ec.message());
  }

#ifndef _WIN32
  std::filesystem::permissions(path, kDirectoryOwnerOnly,
                               std::filesystem::perm_options::replace, ec);
  if (ec) {
    throw SQLiteStateStoreError("SQLiteStateStore: cannot set permissions on " +
                                path.string() + ": " + ec.message());
  }
#endif
}

void EnsurePrivateFile(const std::filesystem::path& path) {
#ifndef _WIN32
  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return;
  }
  std::filesystem::permissions(path, kFileOwnerOnly,
                               std::filesystem::perm_options::replace, ec);
  if (ec) {
    throw SQLiteStateStoreError("SQLiteStateStore: cannot set permissions on " +
                                path.string() + ": " + ec.message());
  }
#else
  (void)path;
#endif
}

class Statement {
 public:
  Statement(sqlite3* db, std::string_view sql) : db_(db) {
    EnsureOk(
        db_,
        sqlite3_prepare_v2(db_, std::string(sql).c_str(), -1, &stmt_, nullptr),
        "prepare statement");
  }

  ~Statement() {
    if (stmt_ != nullptr) {
      sqlite3_finalize(stmt_);
    }
  }
  Statement(const Statement&) = delete;
  Statement& operator=(const Statement&) = delete;
  Statement(Statement&&) = delete;
  Statement& operator=(Statement&&) = delete;

  void BindText(int index, std::string_view value) {
    EnsureOk(
        db_,
        sqlite3_bind_text(stmt_, index, value.data(),
                          static_cast<int>(value.size()), SQLITE_TRANSIENT),
        "bind text");
  }

  void BindOptionalText(int index, const std::optional<std::string>& value) {
    if (value.has_value()) {
      BindText(index, *value);
      return;
    }
    EnsureOk(db_, sqlite3_bind_null(stmt_, index), "bind null");
  }

  void BindOptionalView(int index, std::optional<std::string_view> value) {
    if (value.has_value()) {
      BindText(index, *value);
      return;
    }
    EnsureOk(db_, sqlite3_bind_null(stmt_, index), "bind null");
  }

  void BindInt(int index, int value) {
    EnsureOk(db_, sqlite3_bind_int(stmt_, index, value), "bind int");
  }

  [[nodiscard]] int Step() { return sqlite3_step(stmt_); }

  void StepDone(std::string_view action) { EnsureDone(db_, Step(), action); }

  [[nodiscard]] sqlite3_stmt* Get() const { return stmt_; }

 private:
  sqlite3* db_ = nullptr;
  sqlite3_stmt* stmt_ = nullptr;
};

void Exec(sqlite3* db, std::string_view sql, std::string_view action) {
  char* error = nullptr;
  const int rc =
      sqlite3_exec(db, std::string(sql).c_str(), nullptr, nullptr, &error);
  if (rc != SQLITE_OK) {
    std::string message = error == nullptr ? SqliteMessage(db) : error;
    sqlite3_free(error);
    if (message.find("file is not a database") != std::string::npos ||
        message.find("malformed") != std::string::npos) {
      message =
          "SQLite state database appears corrupt. move "
          "~/.yac/state.sqlite aside and restart YAC to recreate it.";
    } else if (message.find("database is locked") != std::string::npos ||
               message.find("database table is locked") != std::string::npos) {
      message =
          "SQLite state database is locked after waiting up to 5000ms. "
          "Close other YAC processes or inspect ~/.yac/state.sqlite.";
    }
    throw SQLiteStateStoreError("SQLiteStateStore: " + std::string(action) +
                                ": " + message);
  }
}

[[nodiscard]] int QueryInt(sqlite3* db, std::string_view sql,
                           std::string_view action) {
  Statement stmt(db, sql);
  const int rc = stmt.Step();
  if (rc != SQLITE_ROW) {
    ThrowSqlite(db, action);
  }
  return sqlite3_column_int(stmt.Get(), 0);
}

[[nodiscard]] std::string ColumnText(sqlite3_stmt* stmt, int column) {
  const unsigned char* value = sqlite3_column_text(stmt, column);
  if (value == nullptr) {
    return "";
  }
  return reinterpret_cast<const char*>(value);
}

[[nodiscard]] std::optional<std::string> ColumnOptionalText(sqlite3_stmt* stmt,
                                                            int column) {
  if (sqlite3_column_type(stmt, column) == SQLITE_NULL) {
    return std::nullopt;
  }
  return ColumnText(stmt, column);
}

void BeginTransaction(sqlite3* db) {
  Exec(db, "BEGIN IMMEDIATE", "begin migration transaction");
}

void Rollback(sqlite3* db) {
  char* error = nullptr;
  sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, &error);
  sqlite3_free(error);
}

void CreateSchema(sqlite3* db) {
  Exec(db,
       "CREATE TABLE IF NOT EXISTS provider_profiles("
       "profile_id TEXT PRIMARY KEY,"
       "provider_id TEXT NOT NULL,"
       "display_name TEXT NOT NULL,"
       "base_url TEXT,"
       "default_model TEXT,"
       "options_json TEXT NOT NULL DEFAULT '{}',"
       "enabled INTEGER NOT NULL DEFAULT 1,"
       "created_at TEXT NOT NULL,"
       "updated_at TEXT NOT NULL)",
       "create provider_profiles");
  Exec(db,
       "CREATE TABLE IF NOT EXISTS provider_credentials("
       "provider_id TEXT NOT NULL,"
       "profile_id TEXT,"
       "credential_type TEXT NOT NULL CHECK (credential_type IN "
       "('api_key','openai_auth')),"
       "secret_json TEXT NOT NULL,"
       "source TEXT NOT NULL,"
       "created_at TEXT NOT NULL,"
       "updated_at TEXT NOT NULL,"
       "PRIMARY KEY(provider_id, profile_id, credential_type))",
       "create provider_credentials");
  Exec(db,
       "CREATE TABLE IF NOT EXISTS app_state("
       "key TEXT PRIMARY KEY,"
       "value TEXT NOT NULL,"
       "updated_at TEXT NOT NULL)",
       "create app_state");
}

void Migrate(sqlite3* db) {
  const int version = QueryInt(db, "PRAGMA user_version", "read user_version");
  if (version == kSchemaVersion) {
    return;
  }
  if (version != 0) {
    throw SQLiteStateStoreError(
        "SQLiteStateStore: unsupported state schema version " +
        std::to_string(version));
  }

  BeginTransaction(db);
  try {
    CreateSchema(db);
    Exec(db, "PRAGMA user_version=1", "set user_version");
    Exec(db, "COMMIT", "commit migration");
  } catch (...) {
    Rollback(db);
    throw;
  }
}

void ApplyConnectionPolicy(sqlite3* db) {
  Exec(db, "PRAGMA busy_timeout=5000", "set busy_timeout");
  Exec(db, "PRAGMA foreign_keys=ON", "set foreign_keys");
  Exec(db, "PRAGMA journal_mode=WAL", "set journal_mode");
}

[[nodiscard]] ProviderProfile ProfileFromRow(sqlite3_stmt* stmt) {
  return ProviderProfile{.profile_id = ColumnText(stmt, 0),
                         .provider_id = ProviderId{ColumnText(stmt, 1)},
                         .display_name = ColumnText(stmt, 2),
                         .base_url = ColumnOptionalText(stmt, 3),
                         .default_model = [&]() -> std::optional<ModelId> {
                           auto value = ColumnOptionalText(stmt, 4);
                           if (!value) {
                             return std::nullopt;
                           }
                           return ModelId{*value};
                         }(),
                         .options_json = ColumnText(stmt, 5),
                         .enabled = sqlite3_column_int(stmt, 6) != 0,
                         .created_at = ColumnText(stmt, 7),
                         .updated_at = ColumnText(stmt, 8)};
}

[[nodiscard]] ProviderCredential CredentialFromRow(sqlite3_stmt* stmt) {
  const std::string credential_type_text = ColumnText(stmt, 2);
  const auto credential_type = CredentialTypeFromString(credential_type_text);
  if (!credential_type) {
    throw SQLiteStateStoreError("SQLiteStateStore: unknown credential type '" +
                                credential_type_text + "'");
  }
  const std::string source_text = ColumnText(stmt, 4);
  const auto source = CredentialSourceFromString(source_text);
  if (!source) {
    throw SQLiteStateStoreError(
        "SQLiteStateStore: unknown credential source '" + source_text + "'");
  }

  return ProviderCredential{.provider_id = ProviderId{ColumnText(stmt, 0)},
                            .profile_id = ColumnOptionalText(stmt, 1),
                            .credential_type = *credential_type,
                            .secret_json = ColumnText(stmt, 3),
                            .source = *source,
                            .created_at = ColumnText(stmt, 5),
                            .updated_at = ColumnText(stmt, 6)};
}

[[nodiscard]] AppStateEntry AppStateFromRow(sqlite3_stmt* stmt) {
  return AppStateEntry{.key = ColumnText(stmt, 0),
                       .value = ColumnText(stmt, 1),
                       .updated_at = ColumnText(stmt, 2)};
}

}  // namespace

SQLiteStateStore::SQLiteStateStore() : SQLiteStateStore(StateDatabasePath()) {}

SQLiteStateStore::SQLiteStateStore(std::filesystem::path database_path)
    : database_path_(std::move(database_path)) {
  EnsurePrivateDirectory(database_path_.parent_path());
  const int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
  const int rc = sqlite3_open_v2(database_path_.c_str(), &db_, flags, nullptr);
  if (rc != SQLITE_OK) {
    const std::string message = SqliteMessage(db_);
    sqlite3_close(db_);
    db_ = nullptr;
    throw SQLiteStateStoreError("SQLiteStateStore: open database: " + message);
  }
  EnsurePrivateFile(database_path_);
  ApplyConnectionPolicy(db_);
  Migrate(db_);
  EnsurePrivateFile(database_path_);
  EnsurePrivateFile(std::filesystem::path(database_path_.string() + "-wal"));
  EnsurePrivateFile(std::filesystem::path(database_path_.string() + "-shm"));
}

SQLiteStateStore::~SQLiteStateStore() {
  if (db_ != nullptr) {
    sqlite3_close(db_);
  }
}

void SQLiteStateStore::SaveProviderProfile(const ProviderProfile& profile) {
  Statement stmt(
      db_,
      "INSERT INTO provider_profiles(profile_id, provider_id, display_name, "
      "base_url, default_model, options_json, enabled, created_at, "
      "updated_at) VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9) "
      "ON CONFLICT(profile_id) DO UPDATE SET provider_id=excluded.provider_id, "
      "display_name=excluded.display_name, base_url=excluded.base_url, "
      "default_model=excluded.default_model, "
      "options_json=excluded.options_json, "
      "enabled=excluded.enabled, created_at=excluded.created_at, "
      "updated_at=excluded.updated_at");
  stmt.BindText(1, profile.profile_id);
  stmt.BindText(2, profile.provider_id.value);
  stmt.BindText(3, profile.display_name);
  stmt.BindOptionalText(4, profile.base_url);
  if (profile.default_model.has_value()) {
    stmt.BindText(5, profile.default_model->value);
  } else {
    stmt.BindOptionalText(5, std::nullopt);
  }
  stmt.BindText(6, profile.options_json);
  stmt.BindInt(7, profile.enabled ? 1 : 0);
  stmt.BindText(8, profile.created_at);
  stmt.BindText(9, profile.updated_at);
  stmt.StepDone("save provider profile");
}

std::optional<ProviderProfile> SQLiteStateStore::LoadProviderProfile(
    std::string_view profile_id) const {
  Statement stmt(db_,
                 "SELECT profile_id, provider_id, display_name, base_url, "
                 "default_model, options_json, enabled, created_at, updated_at "
                 "FROM provider_profiles WHERE profile_id=?1");
  stmt.BindText(1, profile_id);
  const int rc = stmt.Step();
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    ThrowSqlite(db_, "load provider profile");
  }
  return ProfileFromRow(stmt.Get());
}

std::vector<ProviderProfile> SQLiteStateStore::ListProviderProfiles() const {
  Statement stmt(db_,
                 "SELECT profile_id, provider_id, display_name, base_url, "
                 "default_model, options_json, enabled, created_at, updated_at "
                 "FROM provider_profiles ORDER BY profile_id");
  std::vector<ProviderProfile> profiles;
  for (;;) {
    const int rc = stmt.Step();
    if (rc == SQLITE_DONE) {
      return profiles;
    }
    if (rc != SQLITE_ROW) {
      ThrowSqlite(db_, "list provider profiles");
    }
    profiles.push_back(ProfileFromRow(stmt.Get()));
  }
}

void SQLiteStateStore::DeleteProviderProfile(std::string_view profile_id) {
  Statement stmt(db_, "DELETE FROM provider_profiles WHERE profile_id=?1");
  stmt.BindText(1, profile_id);
  stmt.StepDone("delete provider profile");
}

void SQLiteStateStore::SaveProviderCredential(
    const ProviderCredential& credential) {
  Exec(db_, "BEGIN IMMEDIATE", "begin credential save");
  try {
    DeleteProviderCredential(credential.provider_id, credential.profile_id,
                             credential.credential_type);
    Statement stmt(
        db_,
        "INSERT INTO provider_credentials(provider_id, profile_id, "
        "credential_type, secret_json, source, created_at, updated_at) "
        "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7)");
    stmt.BindText(1, credential.provider_id.value);
    stmt.BindOptionalText(2, credential.profile_id);
    stmt.BindText(3, ToString(credential.credential_type));
    stmt.BindText(4, credential.secret_json);
    stmt.BindText(5, ToString(credential.source));
    stmt.BindText(6, credential.created_at);
    stmt.BindText(7, credential.updated_at);
    stmt.StepDone("save provider credential");
    Exec(db_, "COMMIT", "commit credential save");
  } catch (...) {
    Rollback(db_);
    throw;
  }
}

std::optional<ProviderCredential> SQLiteStateStore::LoadProviderCredential(
    const ProviderId& provider_id, std::optional<std::string_view> profile_id,
    StateCredentialType credential_type) const {
  const bool has_profile = profile_id.has_value();
  Statement stmt(
      db_,
      has_profile
          ? "SELECT provider_id, profile_id, credential_type, secret_json, "
            "source, created_at, updated_at FROM provider_credentials "
            "WHERE provider_id=?1 AND profile_id=?2 AND "
            "credential_type=?3"
          : "SELECT provider_id, profile_id, credential_type, secret_json, "
            "source, created_at, updated_at FROM provider_credentials "
            "WHERE provider_id=?1 AND profile_id IS NULL AND "
            "credential_type=?3");
  stmt.BindText(1, provider_id.value);
  if (has_profile) {
    stmt.BindOptionalView(2, profile_id);
  }
  stmt.BindText(3, ToString(credential_type));
  const int rc = stmt.Step();
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    ThrowSqlite(db_, "load provider credential");
  }
  return CredentialFromRow(stmt.Get());
}

std::vector<ProviderCredential> SQLiteStateStore::ListProviderCredentials()
    const {
  Statement stmt(
      db_,
      "SELECT provider_id, profile_id, credential_type, secret_json, "
      "source, created_at, updated_at FROM provider_credentials "
      "ORDER BY provider_id, coalesce(profile_id, ''), "
      "credential_type");
  std::vector<ProviderCredential> credentials;
  for (;;) {
    const int rc = stmt.Step();
    if (rc == SQLITE_DONE) {
      return credentials;
    }
    if (rc != SQLITE_ROW) {
      ThrowSqlite(db_, "list provider credentials");
    }
    credentials.push_back(CredentialFromRow(stmt.Get()));
  }
}

void SQLiteStateStore::DeleteProviderCredential(
    const ProviderId& provider_id, std::optional<std::string_view> profile_id,
    StateCredentialType credential_type) {
  const bool has_profile = profile_id.has_value();
  Statement stmt(db_, has_profile ? "DELETE FROM provider_credentials WHERE "
                                    "provider_id=?1 AND profile_id=?2 AND "
                                    "credential_type=?3"
                                  : "DELETE FROM provider_credentials WHERE "
                                    "provider_id=?1 AND profile_id IS NULL AND "
                                    "credential_type=?3");
  stmt.BindText(1, provider_id.value);
  if (has_profile) {
    stmt.BindOptionalView(2, profile_id);
  }
  stmt.BindText(3, ToString(credential_type));
  stmt.StepDone("delete provider credential");
}

void SQLiteStateStore::SaveAppState(const AppStateEntry& entry) {
  Statement stmt(db_,
                 "INSERT INTO app_state(key, value, updated_at) "
                 "VALUES(?1, ?2, ?3) ON CONFLICT(key) DO UPDATE SET "
                 "value=excluded.value, updated_at=excluded.updated_at");
  stmt.BindText(1, entry.key);
  stmt.BindText(2, entry.value);
  stmt.BindText(3, entry.updated_at);
  stmt.StepDone("save app state");
}

std::optional<AppStateEntry> SQLiteStateStore::LoadAppState(
    std::string_view key) const {
  Statement stmt(db_,
                 "SELECT key, value, updated_at FROM app_state WHERE key=?1");
  stmt.BindText(1, key);
  const int rc = stmt.Step();
  if (rc == SQLITE_DONE) {
    return std::nullopt;
  }
  if (rc != SQLITE_ROW) {
    ThrowSqlite(db_, "load app state");
  }
  return AppStateFromRow(stmt.Get());
}

std::vector<AppStateEntry> SQLiteStateStore::ListAppState() const {
  Statement stmt(db_,
                 "SELECT key, value, updated_at FROM app_state ORDER BY key");
  std::vector<AppStateEntry> entries;
  for (;;) {
    const int rc = stmt.Step();
    if (rc == SQLITE_DONE) {
      return entries;
    }
    if (rc != SQLITE_ROW) {
      ThrowSqlite(db_, "list app state");
    }
    entries.push_back(AppStateFromRow(stmt.Get()));
  }
}

void SQLiteStateStore::DeleteAppState(std::string_view key) {
  Statement stmt(db_, "DELETE FROM app_state WHERE key=?1");
  stmt.BindText(1, key);
  stmt.StepDone("delete app state");
}

}  // namespace yac::chat
