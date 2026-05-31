#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/sqlite_state_store.hpp"
#include "chat/state_store.hpp"
#include "config_env_test_helpers.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::chat::AppStateEntry;
using yac::chat::ChatConfig;
using yac::chat::ChatService;
using yac::chat::ConfigValueSource;
using yac::chat::LoadChatConfigResultFrom;
using yac::chat::ProviderCredential;
using yac::chat::ProviderProfile;
using yac::chat::ReasoningEffort;
using yac::chat::SQLiteStateStore;
using yac::chat::StateCredentialType;
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
  void DeleteAppState(std::string_view key) override { (void)key; }
};

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::trunc);
  stream << content;
}

ChatConfig RuntimeConfig(std::shared_ptr<yac::chat::StateStore> store) {
  ChatConfig config;
  config.provider_id = yac::ProviderId{"openai-compatible"};
  config.model = yac::ModelId{"gpt-4o"};
  config.state_store = std::move(store);
  return config;
}

ProviderProfile Profile(std::string profile_id, std::string provider_id,
                        std::string model) {
  return ProviderProfile{.profile_id = std::move(profile_id),
                         .provider_id = yac::ProviderId{std::move(provider_id)},
                         .display_name = "Profile",
                         .base_url = "https://profile.example.invalid/v1",
                         .default_model = yac::ModelId{std::move(model)},
                         .options_json = "{}",
                         .enabled = true,
                         .created_at = "2026-05-30T00:00:00Z",
                         .updated_at = "2026-05-30T00:00:00Z"};
}

bool HasSettingFor(const ChatConfig& config, std::string_view provider_id,
                   std::string_view model) {
  return std::ranges::any_of(
      config.model_settings, [&](const yac::chat::ProviderModelSettings& item) {
        return item.provider_id.value == provider_id && item.model.value == model;
      });
}

}

TEST_CASE("LastModelRestart runtime provider model and effort survive restart") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_runtime_state_restart");
  const auto settings_path = dir.Path() / "settings.toml";
  const auto database_path = dir.Path() / "state.sqlite";
  WriteFile(settings_path, "");

  {
    auto state_store = std::make_shared<SQLiteStateStore>(database_path);
    ChatService service({}, RuntimeConfig(state_store));

    service.SetProvider(yac::ProviderId{"openai"});
    service.SetModel(yac::ModelId{"gpt-5.5"});
    service.SetReasoningEffort(ReasoningEffort::High);

    REQUIRE(state_store->LoadAppState(yac::chat::kAppStateLastProviderId)
                ->value == "openai");
    REQUIRE(state_store->LoadAppState(yac::chat::kAppStateLastModel)->value ==
            "gpt-5.5");
    REQUIRE(state_store
                ->LoadAppState(yac::chat::LastModelEffortAppStateKey(
                    yac::ProviderId{"openai"}, yac::ModelId{"gpt-5.5"}))
                ->value == "high");
  }

  SQLiteStateStore restart_store(database_path);
  const auto loaded =
      LoadChatConfigResultFrom(settings_path, false, &restart_store).config;

  REQUIRE(loaded.provider_id.value == "openai");
  REQUIRE(loaded.model.value == "gpt-5.5");
  REQUIRE(loaded.model_settings.size() == 1);
  REQUIRE(loaded.model_settings[0].provider_id.value == "openai");
  REQUIRE(loaded.model_settings[0].model.value == "gpt-5.5");
  REQUIRE(loaded.model_settings[0].effort == ReasoningEffort::High);
  REQUIRE(loaded.source.provider_id == ConfigValueSource::StateStore);
  REQUIRE(loaded.source.model == ConfigValueSource::StateStore);
}

TEST_CASE("TOML and env override persisted runtime state") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_runtime_state_overrides");
  const auto settings_path = dir.Path() / "settings.toml";
  const auto database_path = dir.Path() / "state.sqlite";
  {
    auto state_store = std::make_shared<SQLiteStateStore>(database_path);
    ChatService service({}, RuntimeConfig(state_store));
    service.SetProvider(yac::ProviderId{"openai"});
    service.SetModel(yac::ModelId{"gpt-5.5"});
    service.SetReasoningEffort(ReasoningEffort::High);
  }

  SECTION("TOML provider model and effort remain authoritative") {
    WriteFile(settings_path,
              "[provider]\n"
              "id = \"zai\"\n"
              "model = \"toml-model\"\n"
              "[[provider.model_settings]]\n"
              "provider = \"zai\"\n"
              "model = \"toml-model\"\n"
              "effort = \"medium\"\n");
    SQLiteStateStore restart_store(database_path);

    const auto loaded =
        LoadChatConfigResultFrom(settings_path, false, &restart_store).config;

    REQUIRE(loaded.provider_id.value == "zai");
    REQUIRE(loaded.model.value == "toml-model");
    REQUIRE(loaded.source.provider_id == ConfigValueSource::Toml);
    REQUIRE(loaded.source.model == ConfigValueSource::Toml);
    REQUIRE(loaded.model_settings.size() == 1);
    REQUIRE(loaded.model_settings[0].provider_id.value == "zai");
    REQUIRE(loaded.model_settings[0].model.value == "toml-model");
    REQUIRE(loaded.model_settings[0].effort == ReasoningEffort::Medium);
    REQUIRE_FALSE(HasSettingFor(loaded, "openai", "gpt-5.5"));
  }

  SECTION("TOML provider without model keeps provider and uses runtime model") {
    WriteFile(settings_path,
              "[provider]\n"
              "id = \"zai\"\n");
    SQLiteStateStore restart_store(database_path);

    const auto loaded =
        LoadChatConfigResultFrom(settings_path, false, &restart_store).config;

    REQUIRE(loaded.provider_id.value == "zai");
    REQUIRE(loaded.model.value == "gpt-5.5");
    REQUIRE(loaded.source.provider_id == ConfigValueSource::Toml);
    REQUIRE(loaded.source.model == ConfigValueSource::StateStore);
  }

  SECTION("env provider without model keeps provider and uses runtime model") {
    WriteFile(settings_path, "");
    ScopedEnvVar provider("YAC_PROVIDER", "bedrock");
    SQLiteStateStore restart_store(database_path);

    const auto loaded =
        LoadChatConfigResultFrom(settings_path, false, &restart_store).config;

    REQUIRE(loaded.provider_id.value == "bedrock");
    REQUIRE(loaded.model.value == "gpt-5.5");
    REQUIRE(loaded.source.provider_id == ConfigValueSource::Environment);
    REQUIRE(loaded.source.model == ConfigValueSource::StateStore);
  }

  SECTION("env provider and model beat TOML and runtime state") {
    WriteFile(settings_path,
              "[provider]\n"
              "id = \"zai\"\n"
              "model = \"toml-model\"\n");
    ScopedEnvVar provider("YAC_PROVIDER", "bedrock");
    ScopedEnvVar model("YAC_MODEL", "env-model");
    SQLiteStateStore restart_store(database_path);

    const auto loaded =
        LoadChatConfigResultFrom(settings_path, false, &restart_store).config;

    REQUIRE(loaded.provider_id.value == "bedrock");
    REQUIRE(loaded.model.value == "env-model");
    REQUIRE(loaded.source.provider_id == ConfigValueSource::Environment);
    REQUIRE(loaded.source.model == ConfigValueSource::Environment);
    REQUIRE_FALSE(HasSettingFor(loaded, "openai", "gpt-5.5"));
  }
}

TEST_CASE("config loading does not persist built in default runtime state") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_runtime_state_no_startup_write");
  const auto settings_path = dir.Path() / "settings.toml";
  const auto database_path = dir.Path() / "state.sqlite";
  WriteFile(settings_path, "");
  SQLiteStateStore state_store(database_path);

  const auto loaded =
      LoadChatConfigResultFrom(settings_path, false, &state_store).config;

  REQUIRE(loaded.provider_id.value == "openai-compatible");
  REQUIRE(loaded.model.value == "gpt-4o-mini");
  REQUIRE_FALSE(
      state_store.LoadAppState(yac::chat::kAppStateLastProviderId).has_value());
  REQUIRE_FALSE(
      state_store.LoadAppState(yac::chat::kAppStateLastModel).has_value());
}

TEST_CASE("runtime selection without profile clears stale last profile id") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_runtime_state_stale_profile_clear");
  const auto settings_path = dir.Path() / "settings.toml";
  const auto database_path = dir.Path() / "state.sqlite";
  WriteFile(settings_path, "");

  {
    auto state_store = std::make_shared<SQLiteStateStore>(database_path);
    state_store->SaveProviderProfile(Profile("work", "zai", "profile-model"));
    state_store->SaveAppState(
        AppStateEntry{.key = std::string(yac::chat::kAppStateLastProfileId),
                      .value = "work",
                      .updated_at = "2026-05-30T00:00:00Z"});
    ChatService service({}, RuntimeConfig(state_store));

    service.SetProvider(yac::ProviderId{"openai"});
    service.SetModel(yac::ModelId{"gpt-5.5"});

    REQUIRE_FALSE(
        state_store->LoadAppState(yac::chat::kAppStateLastProfileId)
            .has_value());
  }

  SQLiteStateStore restart_store(database_path);
  const auto loaded =
      LoadChatConfigResultFrom(settings_path, false, &restart_store).config;

  REQUIRE(loaded.provider_id.value == "openai");
  REQUIRE_FALSE(loaded.profile_id.has_value());
  REQUIRE(loaded.model.value == "gpt-5.5");
  REQUIRE(loaded.source.provider_id == ConfigValueSource::StateStore);
  REQUIRE(loaded.source.model == ConfigValueSource::StateStore);
}

TEST_CASE("failed runtime app_state writes do not abort service mutations") {
  auto state_store = std::make_shared<FailingAppStateStore>();
  ChatService service({}, RuntimeConfig(state_store));

  REQUIRE_NOTHROW(service.SetProvider(yac::ProviderId{"openai"}));
  REQUIRE_NOTHROW(service.SetModel(yac::ModelId{"gpt-5.5"}));
  REQUIRE_NOTHROW(service.SetReasoningEffort(ReasoningEffort::Medium));

  const auto snapshot = service.ConfigSnapshot();
  REQUIRE(snapshot.provider_id.value == "openai");
  REQUIRE(snapshot.model.value == "gpt-5.5");
  REQUIRE(snapshot.model_settings.size() == 1);
  REQUIRE(snapshot.model_settings[0].effort == ReasoningEffort::Medium);
}
