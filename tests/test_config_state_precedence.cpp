#include "chat/config.hpp"
#include "chat/state_store.hpp"
#include "chat/types.hpp"
#include "config_env_test_helpers.hpp"
#include "fake_state_store.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::chat::AppStateEntry;
using yac::chat::ConfigValueSource;
using yac::chat::LoadChatConfigResultFrom;
using yac::chat::ProviderProfile;
using yac::chat::ReasoningEffort;
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

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::trunc);
  stream << content;
}

void SaveStateProfile(FakeStateStore& store) {
  store.SaveProviderProfile(ProviderProfile{
      .profile_id = "work",
      .provider_id = yac::ProviderId{"zai"},
      .display_name = "Work profile",
      .base_url = "https://state.example.invalid/v1",
      .default_model = yac::ModelId{"state-model"},
      .options_json = R"({"region":"state-region","max_tokens":"777"})",
      .enabled = true,
      .created_at = "2026-05-30T00:00:00Z",
      .updated_at = "2026-05-30T00:00:00Z"});
  store.SaveAppState(
      AppStateEntry{.key = std::string(yac::chat::kAppStateLastProfileId),
                    .value = "work",
                    .updated_at = "2026-05-30T00:00:00Z"});
  store.SaveAppState(
      AppStateEntry{.key = std::string(yac::chat::kAppStateLastProviderId),
                    .value = "zai",
                    .updated_at = "2026-05-30T00:00:00Z"});
  store.SaveAppState(
      AppStateEntry{.key = std::string(yac::chat::kAppStateLastModel),
                    .value = "state-last-model",
                    .updated_at = "2026-05-30T00:00:00Z"});
  store.SaveAppState(AppStateEntry{
      .key = yac::chat::LastModelEffortAppStateKey(
          yac::ProviderId{"zai"}, yac::ModelId{"state-last-model"}),
      .value = "high",
      .updated_at = "2026-05-30T00:00:00Z"});
}

}  // namespace

TEST_CASE("SQLiteFillsMissing provider profile and runtime state") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_config_state_fills_missing");
  const auto settings_path = dir.Path() / "settings.toml";
  WriteFile(settings_path, "");
  FakeStateStore store;
  SaveStateProfile(store);

  const auto loaded =
      LoadChatConfigResultFrom(settings_path, false, &store).config;

  REQUIRE(loaded.provider_id.value == "zai");
  REQUIRE(loaded.profile_id == "work");
  REQUIRE(loaded.model.value == "state-last-model");
  REQUIRE(loaded.base_url == "https://state.example.invalid/v1");
  REQUIRE(loaded.options.at("region") == "state-region");
  REQUIRE(loaded.options.at("max_tokens") == "777");
  REQUIRE(loaded.model_settings.size() == 1);
  REQUIRE(loaded.model_settings[0].provider_id.value == "zai");
  REQUIRE(loaded.model_settings[0].model.value == "state-last-model");
  REQUIRE(loaded.model_settings[0].effort == ReasoningEffort::High);
  REQUIRE(loaded.source.provider_id == ConfigValueSource::StateStore);
  REQUIRE(loaded.source.profile_id == ConfigValueSource::StateStore);
  REQUIRE(loaded.source.model == ConfigValueSource::StateStore);
  REQUIRE(loaded.source.base_url == ConfigValueSource::StateStore);
  REQUIRE(loaded.source.provider_options.at("region") ==
          ConfigValueSource::StateStore);
  REQUIRE(loaded.source.model_settings ==
          std::vector<ConfigValueSource>{ConfigValueSource::StateStore});
}

TEST_CASE("OverridePrecedence TOML overrides SQLite and env overrides both") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_config_state_override_precedence");
  const auto settings_path = dir.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "model = \"toml-model\"\n"
            "base_url = \"https://toml.example.invalid/v1\"\n"
            "[provider.options]\n"
            "region = \"toml-region\"\n"
            "[[provider.model_settings]]\n"
            "provider = \"openai-compatible\"\n"
            "model = \"toml-model\"\n"
            "effort = \"minimal\"\n");
  FakeStateStore store;
  SaveStateProfile(store);
  ScopedEnvVar provider("YAC_PROVIDER", "bedrock");
  ScopedEnvVar model("YAC_MODEL", "env-model");
  ScopedEnvVar base_url("YAC_BASE_URL", "https://env.example.invalid/v1");
  ScopedEnvVar bedrock_region("YAC_BEDROCK_REGION", "env-region");

  const auto loaded =
      LoadChatConfigResultFrom(settings_path, false, &store).config;

  REQUIRE(loaded.provider_id.value == "bedrock");
  REQUIRE(loaded.model.value == "env-model");
  REQUIRE(loaded.base_url == "https://env.example.invalid/v1");
  REQUIRE(loaded.options.at("region") == "env-region");
  REQUIRE(loaded.model_settings.size() == 1);
  REQUIRE(loaded.model_settings[0].source == ConfigValueSource::Toml);
  REQUIRE(loaded.source.provider_id == ConfigValueSource::Environment);
  REQUIRE(loaded.source.model == ConfigValueSource::Environment);
  REQUIRE(loaded.source.base_url == ConfigValueSource::Environment);
  REQUIRE(loaded.source.provider_options.at("region") ==
          ConfigValueSource::Environment);
}

TEST_CASE("stale SQLite provider and profile state is ignored safely") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_config_state_stale_ignored");
  const auto settings_path = dir.Path() / "settings.toml";
  WriteFile(settings_path, "");
  FakeStateStore store;
  store.SaveAppState(
      AppStateEntry{.key = std::string(yac::chat::kAppStateLastProviderId),
                    .value = "deleted-provider",
                    .updated_at = "2026-05-30T00:00:00Z"});
  store.SaveAppState(
      AppStateEntry{.key = std::string(yac::chat::kAppStateLastProfileId),
                    .value = "deleted-profile",
                    .updated_at = "2026-05-30T00:00:00Z"});
  store.SaveAppState(
      AppStateEntry{.key = std::string(yac::chat::kAppStateLastModel),
                    .value = "deleted-model",
                    .updated_at = "2026-05-30T00:00:00Z"});

  const auto loaded = LoadChatConfigResultFrom(settings_path, false, &store);

  REQUIRE(loaded.config.provider_id.value == "openai-compatible");
  REQUIRE_FALSE(loaded.config.profile_id.has_value());
  REQUIRE(loaded.config.model.value == "gpt-4o-mini");
  REQUIRE(loaded.config.base_url == "https://api.openai.com/v1/");
  REQUIRE(loaded.config.source.provider_id ==
          ConfigValueSource::BuiltInDefault);
  REQUIRE(loaded.config.source.model == ConfigValueSource::BuiltInDefault);
}

TEST_CASE(
    "runtime state load and plaintext keys do not mutate the state store") {
  ScopedEnvClear env_guard({"OPENAI_API_KEY", "ZAI_API_KEY"});
  TempDir dir("yac_test_config_state_no_persist");
  const auto settings_path = dir.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "api_key = \"toml-secret\"\n"
            "api_key_env = \"OPENAI_API_KEY\"\n");
  FakeStateStore store;
  SaveStateProfile(store);
  ScopedEnvVar api_key("OPENAI_API_KEY", "env-secret");

  const auto before_credentials = store.ListProviderCredentials();
  const auto loaded =
      LoadChatConfigResultFrom(settings_path, false, &store).config;
  const auto after_credentials = store.ListProviderCredentials();

  REQUIRE(loaded.provider_id.value == "openai-compatible");
  REQUIRE(loaded.api_key == "toml-secret");
  REQUIRE(loaded.source.api_key == ConfigValueSource::Toml);
  REQUIRE(before_credentials.empty());
  REQUIRE(after_credentials.empty());
  REQUIRE(store.LoadProviderProfile("work")->default_model->value ==
          "state-model");
}
