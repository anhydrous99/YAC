#include "chat/config.hpp"
#include "chat/sqlite_state_store.hpp"
#include "chat/state_store.hpp"
#include "config_env_test_helpers.hpp"
#include "fake_state_store.hpp"

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

using yac::chat::AppStateEntry;
using yac::chat::ConfigValueSource;
using yac::chat::LoadChatConfigResultFrom;
using yac::chat::ProviderCredential;
using yac::chat::ProviderProfile;
using yac::chat::SQLiteStateStore;
using yac::chat::StateCredentialSource;
using yac::chat::StateCredentialType;
using yac::testing::FakeStateStore;
using yac::testing::ScopedEnvClear;

namespace {

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::optional<std::string> value)
      : name_(std::move(name)) {
    if (const char* previous = std::getenv(name_.c_str())) {
      previous_ = previous;
    }
    if (value.has_value()) {
      setenv(name_.c_str(), value->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  ~ScopedEnvVar() {
    if (previous_.has_value()) {
      setenv(name_.c_str(), previous_->c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
  ScopedEnvVar(ScopedEnvVar&&) = delete;
  ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

 private:
  std::string name_;
  std::optional<std::string> previous_;
};

class TempDir {
 public:
  explicit TempDir(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempDir() { std::filesystem::remove_all(path_); }

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

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::trunc);
  stream << content;
}

ProviderProfile Profile(std::string profile_id, std::string provider_id,
                        std::string model, std::string options_json = "{}") {
  return ProviderProfile{.profile_id = std::move(profile_id),
                         .provider_id = yac::ProviderId{std::move(provider_id)},
                         .display_name = "Profile",
                         .base_url = "https://profile.example.invalid/v1",
                         .default_model = yac::ModelId{std::move(model)},
                         .options_json = std::move(options_json),
                         .enabled = true,
                         .created_at = "2026-05-30T00:00:00Z",
                         .updated_at = "2026-05-30T00:00:00Z"};
}

}  // namespace

TEST_CASE(
    "default profile id is provider id when no named profile is selected") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_provider_profiles_default_id");
  const auto settings_path = dir.Path() / "settings.toml";
  WriteFile(settings_path, "");
  FakeStateStore store;
  store.SaveProviderProfile(Profile("openai-compatible", "openai-compatible",
                                    "profile-default-model",
                                    R"({"reasoning_effort":"low"})"));

  const auto loaded = LoadChatConfigResultFrom(settings_path, false, &store);

  REQUIRE(loaded.config.provider_id.value == "openai-compatible");
  REQUIRE(loaded.config.profile_id == "openai-compatible");
  REQUIRE(loaded.config.model.value == "profile-default-model");
  REQUIRE(loaded.config.base_url == "https://profile.example.invalid/v1");
  REQUIRE(loaded.config.options.at("reasoning_effort") == "low");
  REQUIRE(loaded.config.source.profile_id == ConfigValueSource::StateStore);
  REQUIRE(loaded.config.source.model == ConfigValueSource::StateStore);
  REQUIRE(loaded.config.source.base_url == ConfigValueSource::StateStore);
  REQUIRE(loaded.config.source.provider_options.at("reasoning_effort") ==
          ConfigValueSource::StateStore);
}

TEST_CASE(
    "explicit provider selects matching default profile below TOML and env") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_provider_profiles_explicit_provider");
  const auto settings_path = dir.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"bedrock\"\n"
            "base_url = \"https://toml.example.invalid\"\n"
            "[provider.options]\n"
            "region = \"toml-region\"\n"
            "max_tokens = \"12000\"\n");
  FakeStateStore store;
  store.SaveProviderProfile(Profile(
      "bedrock", "bedrock", "profile-bedrock",
      R"({"region":"profile-region","max_tokens":"777","profile":"aws-profile"})"));
  ScopedEnvVar max_tokens("YAC_BEDROCK_MAX_TOKENS", "8192");

  const auto loaded = LoadChatConfigResultFrom(settings_path, false, &store);

  REQUIRE(loaded.config.provider_id.value == "bedrock");
  REQUIRE(loaded.config.profile_id == "bedrock");
  REQUIRE(loaded.config.model.value == "profile-bedrock");
  REQUIRE(loaded.config.base_url == "https://toml.example.invalid");
  REQUIRE(loaded.config.options.at("region") == "toml-region");
  REQUIRE(loaded.config.options.at("max_tokens") == "8192");
  REQUIRE(loaded.config.options.at("profile") == "aws-profile");
  REQUIRE(loaded.config.source.base_url == ConfigValueSource::Toml);
  REQUIRE(loaded.config.source.provider_options.at("region") ==
          ConfigValueSource::Toml);
  REQUIRE(loaded.config.source.provider_options.at("max_tokens") ==
          ConfigValueSource::Environment);
  REQUIRE(loaded.config.source.provider_options.at("profile") ==
          ConfigValueSource::StateStore);
}

TEST_CASE("disabled and unknown profiles are ignored safely") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_provider_profiles_ignored");
  const auto settings_path = dir.Path() / "settings.toml";
  WriteFile(settings_path, "");
  FakeStateStore store;
  auto disabled =
      Profile("openai-compatible", "openai-compatible", "disabled-model");
  disabled.enabled = false;
  store.SaveProviderProfile(disabled);

  SECTION("disabled default profile") {
    const auto loaded = LoadChatConfigResultFrom(settings_path, false, &store);

    REQUIRE(loaded.config.provider_id.value == "openai-compatible");
    REQUIRE_FALSE(loaded.config.profile_id.has_value());
    REQUIRE(loaded.config.model.value == "gpt-4o-mini");
    REQUIRE(loaded.config.source.model == ConfigValueSource::BuiltInDefault);
  }

  SECTION("unknown named profile") {
    store.SaveAppState(
        AppStateEntry{.key = std::string(yac::chat::kAppStateLastProfileId),
                      .value = "deleted-profile",
                      .updated_at = "2026-05-30T00:00:00Z"});

    const auto loaded = LoadChatConfigResultFrom(settings_path, false, &store);

    REQUIRE(loaded.config.provider_id.value == "openai-compatible");
    REQUIRE_FALSE(loaded.config.profile_id.has_value());
    REQUIRE(loaded.config.model.value == "gpt-4o-mini");
  }
}

TEST_CASE("profile options apply independently for each provider style") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  struct Case {
    std::string provider_id;
    std::string option_key;
    std::string option_value;
  };
  const std::vector<Case> cases = {
      {.provider_id = "openai",
       .option_key = "reasoning_effort",
       .option_value = "medium"},
      {.provider_id = "openai-compatible",
       .option_key = "reasoning",
       .option_value = "low"},
      {.provider_id = "zai",
       .option_key = "reasoning_effort",
       .option_value = "high"},
      {.provider_id = "bedrock",
       .option_key = "region",
       .option_value = "us-west-2"},
  };

  for (const auto& test_case : cases) {
    TempDir dir("yac_test_provider_profiles_" + test_case.provider_id);
    const auto settings_path = dir.Path() / "settings.toml";
    WriteFile(settings_path, "");
    FakeStateStore store;
    store.SaveProviderProfile(Profile(test_case.provider_id,
                                      test_case.provider_id, "profile-model",
                                      "{\"" + test_case.option_key + "\":\"" +
                                          test_case.option_value + "\"}"));
    ScopedEnvVar provider("YAC_PROVIDER", test_case.provider_id);

    const auto loaded = LoadChatConfigResultFrom(settings_path, false, &store);

    REQUIRE(loaded.config.provider_id.value == test_case.provider_id);
    REQUIRE(loaded.config.profile_id == test_case.provider_id);
    REQUIRE(loaded.config.options.at(test_case.option_key) ==
            test_case.option_value);
    REQUIRE(loaded.config.source.provider_options.at(test_case.option_key) ==
            ConfigValueSource::StateStore);
  }
}

TEST_CASE(
    "provider profile persistence keeps options JSON separate from secrets") {
  TempDir dir("yac_test_provider_profiles_sqlite");
  const auto db_path = dir.Path() / ".yac" / "state.sqlite";
  const auto profile = Profile("openai", "openai", "gpt-profile",
                               R"({"reasoning_effort":"medium"})");
  const ProviderCredential credential{
      .provider_id = yac::ProviderId{"openai"},
      .profile_id = std::string("openai"),
      .credential_type = StateCredentialType::ApiKey,
      .secret_json = R"({"api_key":"secret-profile-key"})",
      .source = StateCredentialSource::Test,
      .created_at = "2026-05-30T00:00:00Z",
      .updated_at = "2026-05-30T00:00:00Z"};

  {
    SQLiteStateStore store(db_path);
    store.SaveProviderProfile(profile);
    store.SaveProviderCredential(credential);
  }

  SQLiteStateStore reopened(db_path);
  REQUIRE(reopened.LoadProviderProfile("openai") == profile);
  REQUIRE(reopened.LoadProviderCredential(yac::ProviderId{"openai"}, "openai",
                                          StateCredentialType::ApiKey) ==
          credential);

  SqliteDb db(db_path);
  REQUIRE(ScalarText(db.Get(),
                     "SELECT options_json FROM provider_profiles WHERE "
                     "profile_id='openai'") ==
          R"({"reasoning_effort":"medium"})");
  REQUIRE(ScalarText(db.Get(),
                     "SELECT count(*) FROM provider_profiles WHERE "
                     "base_url LIKE '%secret%' OR default_model LIKE "
                     "'%secret%' OR options_json LIKE '%secret%'") == "0");
  REQUIRE(ScalarText(db.Get(),
                     "SELECT secret_json FROM provider_credentials WHERE "
                     "provider_id='openai' AND profile_id='openai'") ==
          R"({"api_key":"secret-profile-key"})");
}
