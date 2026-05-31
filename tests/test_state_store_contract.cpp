#include "chat/settings_toml.hpp"
#include "chat/state_store.hpp"
#include "chat/types.hpp"
#include "fake_state_store.hpp"

#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::chat::AppStateEntry;
using yac::chat::CredentialSourceFromString;
using yac::chat::CredentialTypeFromString;
using yac::chat::ProviderCredential;
using yac::chat::ProviderProfile;
using yac::chat::StateCredentialSource;
using yac::chat::StateCredentialType;
using yac::chat::StateStore;
using yac::chat::ToString;
using yac::testing::FakeStateStore;

namespace {

class TempDir {
 public:
  explicit TempDir(std::string name)
      : path_(std::filesystem::temp_directory_path() / std::move(name)) {
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
  std::ofstream stream(path, std::ios::trunc);
  stream << content;
}

}  // namespace

TEST_CASE("state store contract exposes exact credential constants") {
  REQUIRE(ToString(StateCredentialType::ApiKey) == "api_key");
  REQUIRE(ToString(StateCredentialType::OpenAiAuth) == "openai_auth");
  REQUIRE(CredentialTypeFromString("api_key") == StateCredentialType::ApiKey);
  REQUIRE(CredentialTypeFromString("openai_auth") ==
          StateCredentialType::OpenAiAuth);
  REQUIRE_FALSE(CredentialTypeFromString("oauth_token").has_value());

  REQUIRE(ToString(StateCredentialSource::AuthCli) == "auth_cli");
  REQUIRE(ToString(StateCredentialSource::MigrationLegacyFile) ==
          "migration_legacy_file");
  REQUIRE(ToString(StateCredentialSource::MigrationKeychain) ==
          "migration_keychain");
  REQUIRE(ToString(StateCredentialSource::ManualCli) == "manual_cli");
  REQUIRE(ToString(StateCredentialSource::Test) == "test");
  REQUIRE(CredentialSourceFromString("test") == StateCredentialSource::Test);
  REQUIRE_FALSE(CredentialSourceFromString("unknown").has_value());
}

TEST_CASE("state store reserves app state keys needed by the plan") {
  REQUIRE(yac::chat::kAppStateLastProviderId == "last_provider_id");
  REQUIRE(yac::chat::kAppStateLastProfileId == "last_profile_id");
  REQUIRE(yac::chat::kAppStateLastModel == "last_model");
  REQUIRE(yac::chat::LastModelEffortAppStateKey(
              yac::chat::ProviderId{"openai"}, yac::chat::ModelId{"gpt-5.5"}) ==
          "last_model_effort:openai:gpt-5.5");
}

TEST_CASE("fake state store round-trips provider profiles deterministically") {
  FakeStateStore fake;
  StateStore& store = fake;

  ProviderProfile disabled{.profile_id = "secondary",
                           .provider_id = yac::chat::ProviderId{"zai"},
                           .display_name = "Z.ai Secondary",
                           .base_url = "https://example.invalid/v1",
                           .default_model = yac::chat::ModelId{"glm-5.1"},
                           .options_json = R"({"region":"test"})",
                           .enabled = false,
                           .created_at = "2026-05-30T00:00:00Z",
                           .updated_at = "2026-05-30T00:01:00Z"};
  ProviderProfile primary{.profile_id = "primary",
                          .provider_id = yac::chat::ProviderId{"openai"},
                          .display_name = "OpenAI Primary",
                          .base_url = "https://api.openai.com/v1/",
                          .default_model = yac::chat::ModelId{"gpt-4o-mini"},
                          .options_json = "{}",
                          .enabled = true,
                          .created_at = "2026-05-30T00:00:00Z",
                          .updated_at = "2026-05-30T00:02:00Z"};

  store.SaveProviderProfile(disabled);
  store.SaveProviderProfile(primary);

  REQUIRE(store.LoadProviderProfile("primary") == primary);
  REQUIRE(store.LoadProviderProfile("missing") == std::nullopt);

  const std::vector<ProviderProfile> profiles = store.ListProviderProfiles();
  REQUIRE(profiles == std::vector<ProviderProfile>{primary, disabled});

  store.DeleteProviderProfile("primary");
  REQUIRE_FALSE(store.LoadProviderProfile("primary").has_value());
  REQUIRE(store.ListProviderProfiles() ==
          std::vector<ProviderProfile>{disabled});
}

TEST_CASE("fake state store round-trips credentials by provider profile type") {
  FakeStateStore fake;
  StateStore& store = fake;

  ProviderCredential profile_key{
      .provider_id = yac::chat::ProviderId{"openai-compatible"},
      .profile_id = "primary",
      .credential_type = StateCredentialType::ApiKey,
      .secret_json = R"({"api_key":"sk-test"})",
      .source = StateCredentialSource::Test,
      .created_at = "2026-05-30T00:00:00Z",
      .updated_at = "2026-05-30T00:01:00Z"};
  ProviderCredential provider_auth{
      .provider_id = yac::chat::ProviderId{"openai"},
      .profile_id = std::nullopt,
      .credential_type = StateCredentialType::OpenAiAuth,
      .secret_json = R"({"refresh":"token"})",
      .source = StateCredentialSource::AuthCli,
      .created_at = "2026-05-30T00:00:00Z",
      .updated_at = "2026-05-30T00:02:00Z"};

  store.SaveProviderCredential(profile_key);
  store.SaveProviderCredential(provider_auth);

  REQUIRE(store.LoadProviderCredential(
              yac::chat::ProviderId{"openai-compatible"}, "primary",
              StateCredentialType::ApiKey) == profile_key);
  REQUIRE(store.LoadProviderCredential(
              yac::chat::ProviderId{"openai"}, std::nullopt,
              StateCredentialType::OpenAiAuth) == provider_auth);

  const auto credentials = store.ListProviderCredentials();
  REQUIRE(credentials ==
          std::vector<ProviderCredential>{provider_auth, profile_key});

  store.DeleteProviderCredential(yac::chat::ProviderId{"openai-compatible"},
                                 "primary", StateCredentialType::ApiKey);
  REQUIRE_FALSE(
      store
          .LoadProviderCredential(yac::chat::ProviderId{"openai-compatible"},
                                  "primary", StateCredentialType::ApiKey)
          .has_value());
}

TEST_CASE("fake state store round-trips app state values deterministically") {
  FakeStateStore fake;
  StateStore& store = fake;

  store.SaveAppState(
      AppStateEntry{.key = std::string(yac::chat::kAppStateLastModel),
                    .value = "gpt-4o-mini",
                    .updated_at = "2026-05-30T00:01:00Z"});
  store.SaveAppState(
      AppStateEntry{.key = std::string(yac::chat::kAppStateLastProviderId),
                    .value = "openai-compatible",
                    .updated_at = "2026-05-30T00:00:00Z"});

  REQUIRE(store.LoadAppState(yac::chat::kAppStateLastProviderId)->value ==
          "openai-compatible");
  REQUIRE(store.LoadAppState("missing") == std::nullopt);
  REQUIRE(store.ListAppState() ==
          std::vector<AppStateEntry>{
              {.key = std::string(yac::chat::kAppStateLastModel),
               .value = "gpt-4o-mini",
               .updated_at = "2026-05-30T00:01:00Z"},
              {.key = std::string(yac::chat::kAppStateLastProviderId),
               .value = "openai-compatible",
               .updated_at = "2026-05-30T00:00:00Z"}});

  store.DeleteAppState(yac::chat::kAppStateLastModel);
  REQUIRE_FALSE(store.LoadAppState(yac::chat::kAppStateLastModel).has_value());
}

TEST_CASE(
    "chat config carries source-aware metadata without changing defaults") {
  yac::chat::ChatConfig config;

  REQUIRE(config.provider_id.value == "openai-compatible");
  REQUIRE(config.model.value == "gpt-4o-mini");
  REQUIRE(config.source.provider_id ==
          yac::chat::ConfigValueSource::BuiltInDefault);
  REQUIRE(config.source.model == yac::chat::ConfigValueSource::BuiltInDefault);
  REQUIRE(config.source.base_url ==
          yac::chat::ConfigValueSource::BuiltInDefault);
  REQUIRE(config.source.api_key ==
          yac::chat::ConfigValueSource::BuiltInDefault);
  REQUIRE(config.source.api_key_env ==
          yac::chat::ConfigValueSource::BuiltInDefault);
  REQUIRE(config.source.provider_options.empty());
  REQUIRE(config.source.model_settings.empty());
}

TEST_CASE("settings TOML loader marks explicit provider sources") {
  TempDir dir("yac_test_state_store_contract_sources");
  const auto settings_path = dir.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"bedrock\"\n"
            "model = \"anthropic.claude-3-5-haiku-20241022-v1:0\"\n"
            "base_url = \"https://bedrock-runtime.example.invalid\"\n"
            "api_key = \"plaintext-test-only\"\n"
            "api_key_env = \"BEDROCK_TEST_KEY\"\n"
            "[provider.options]\n"
            "region = \"us-west-2\"\n"
            "max_tokens = 1024\n"
            "[[provider.model_settings]]\n"
            "provider = \"bedrock\"\n"
            "model = \"anthropic.claude-3-5-haiku-20241022-v1:0\"\n"
            "effort = \"high\"\n");

  yac::chat::ChatConfig config;
  std::vector<yac::chat::ConfigIssue> issues;
  const auto fields =
      yac::chat::LoadSettingsFromToml(settings_path, config, issues);

  REQUIRE(issues.empty());
  REQUIRE(fields.provider_id);
  REQUIRE(fields.model);
  REQUIRE(fields.base_url);
  REQUIRE(fields.api_key);
  REQUIRE(fields.api_key_env);
  REQUIRE(fields.model_settings);
  REQUIRE(config.source.provider_id == yac::chat::ConfigValueSource::Toml);
  REQUIRE(config.source.model == yac::chat::ConfigValueSource::Toml);
  REQUIRE(config.source.base_url == yac::chat::ConfigValueSource::Toml);
  REQUIRE(config.source.api_key == yac::chat::ConfigValueSource::Toml);
  REQUIRE(config.source.api_key_env == yac::chat::ConfigValueSource::Toml);
  REQUIRE(config.source.provider_options.at("region") ==
          yac::chat::ConfigValueSource::Toml);
  REQUIRE(config.source.provider_options.at("max_tokens") ==
          yac::chat::ConfigValueSource::Toml);
  REQUIRE(config.source.model_settings ==
          std::vector<yac::chat::ConfigValueSource>{
              yac::chat::ConfigValueSource::Toml});
  REQUIRE(config.model_settings[0].source ==
          yac::chat::ConfigValueSource::Toml);
}
