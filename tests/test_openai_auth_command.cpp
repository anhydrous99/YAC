#include "cli/provider_auth_command.hpp"

#include "provider/openai_auth_store.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

namespace {

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
    if (const char* current = std::getenv(name_.c_str())) {
      prior_ = std::string(current);
    }
    setenv(name_.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (prior_.has_value()) {
      setenv(name_.c_str(), prior_->c_str(), 1);
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
  std::optional<std::string> prior_;
};

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

class MemoryAuthBackend : public yac::provider::IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    std::scoped_lock lock(mutex_);
    return value_;
  }

  void Set(std::string_view auth_json) override {
    std::scoped_lock lock(mutex_);
    value_ = std::string(auth_json);
  }

  void Erase() override {
    std::scoped_lock lock(mutex_);
    value_.reset();
  }

 private:
  mutable std::mutex mutex_;
  std::optional<std::string> value_;
};

class ThrowingKeychainBackend : public yac::provider::IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    throw yac::provider::OpenAiAuthKeychainUnavailableError(
        "keychain unavailable");
  }

  void Set(std::string_view auth_json) override {
    (void)auth_json;
    throw yac::provider::OpenAiAuthKeychainUnavailableError(
        "keychain unavailable");
  }

  void Erase() override {
    throw yac::provider::OpenAiAuthKeychainUnavailableError(
        "keychain unavailable");
  }
};

std::shared_ptr<yac::provider::OpenAiAuthStore> MakeStore(
    const std::shared_ptr<MemoryAuthBackend>& file_backend) {
  return std::make_shared<yac::provider::OpenAiAuthStore>(
      yac::provider::OpenAiAuthStore::Dependencies{
          .keychain_backend = std::make_shared<ThrowingKeychainBackend>(),
          .file_backend = file_backend,
      });
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  output << content;
}

std::string AllWarnings(const yac::cli::ProviderAuthStatusSummary& summary) {
  std::string joined;
  for (const auto& warning : summary.warnings) {
    joined += warning;
    joined += '\n';
  }
  return joined;
}

}  // namespace

TEST_CASE("openai login stores oauth auth", "[openai_auth_command]") {
  TempDir tmp("yac_test_openai_auth_command_login");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "api_key_env = \"OPENAI_API_KEY\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  bool called = false;
  yac::cli::ProviderAuthCommand cmd({
      .settings_path = settings_path,
      .auth_store = store,
      .login_fn = [&called](const yac::provider::OpenAiAuthorizationObserver& observer) {
        called = true;
        observer(yac::provider::OpenAiAuthorizationNotice{
            .authorization_url = "https://auth.openai.com/oauth/authorize?state=abc",
            .redirect_uri = "http://127.0.0.1:1455/auth/callback",
            .browser_launched = true,
        });
        return yac::provider::OpenAiOAuthAuth{
            .refresh_token = "refresh-token",
            .access_token = "access-token",
            .expires_at = std::chrono::system_clock::time_point{
                std::chrono::seconds{4102444800}},
            .account_id = std::string("acct-123"),
        };
      },
  });

  const auto result = cmd.LoginOpenAi();

  REQUIRE(called);
  REQUIRE(result.browser_launched);
  REQUIRE_FALSE(result.authorization_url.has_value());
  const auto stored = store->Load();
  REQUIRE(stored.has_value());
  const auto* oauth = std::get_if<yac::provider::OpenAiOAuthAuth>(&stored->auth);
  REQUIRE(oauth != nullptr);
  REQUIRE(oauth->refresh_token == "refresh-token");
  REQUIRE(oauth->account_id == std::optional<std::string>{"acct-123"});
}

TEST_CASE("openai login surfaces flow failure", "[openai_auth_command]") {
  TempDir tmp("yac_test_openai_auth_command_login_fail");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path, "[provider]\nid = \"openai-compatible\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  yac::cli::ProviderAuthCommand cmd({
      .settings_path = settings_path,
      .auth_store = store,
      .login_fn = [](const yac::provider::OpenAiAuthorizationObserver&) -> yac::provider::OpenAiOAuthAuth {
        throw std::runtime_error("oauth failed");
      },
  });

  REQUIRE_THROWS_WITH(cmd.LoginOpenAi(), "oauth failed");
  REQUIRE_FALSE(store->Load().has_value());
}

TEST_CASE("set api key reads one line and stores api auth",
          "[openai_auth_command]") {
  TempDir tmp("yac_test_openai_auth_command_api");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path, "[provider]\nid = \"openai-compatible\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  std::istringstream input("sk-test\nsecond-line\n");
  yac::cli::ProviderAuthCommand cmd({
      .settings_path = settings_path,
      .auth_store = store,
      .in = &input,
  });

  const auto source = cmd.SetOpenAiApiKeyFromStdin();

  REQUIRE(source == yac::provider::OpenAiAuthStorageSource::File);
  const auto stored = store->Load();
  REQUIRE(stored.has_value());
  const auto* api = std::get_if<yac::provider::OpenAiApiKeyAuth>(&stored->auth);
  REQUIRE(api != nullptr);
  REQUIRE(api->key == "sk-test");
  std::string remaining;
  REQUIRE(std::getline(input, remaining));
  REQUIRE(remaining == "second-line");
}

TEST_CASE("status prefers env over stored and config auth without leaking secrets",
          "[openai_auth_command]") {
  TempDir tmp("yac_test_openai_auth_command_status");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "api_key_env = \"OPENAI_API_KEY\"\n"
            "api_key = \"config-secret\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  static_cast<void>(store->Save(yac::provider::OpenAiOAuthAuth{
      .refresh_token = "refresh-secret",
      .access_token = "access-secret",
      .expires_at = std::chrono::system_clock::time_point{
          std::chrono::seconds{4102444800}},
      .account_id = std::string("acct-xyz"),
  }));
  ScopedEnvVar env("OPENAI_API_KEY", "env-secret");

  yac::cli::ProviderAuthCommand cmd({
      .settings_path = settings_path,
      .auth_store = store,
  });

  const auto summary = cmd.GetOpenAiStatus();

  REQUIRE(summary.configured_provider == "openai-compatible");
  REQUIRE(summary.stored_credential == std::optional<std::string>{"oauth"});
  REQUIRE(summary.effective_auth ==
          std::optional<std::string>{"api (env OPENAI_API_KEY)"});
  REQUIRE(summary.oauth_expiry == std::optional<std::string>{"4102444800"});
  REQUIRE(summary.account_id == std::optional<std::string>{"acct-xyz"});
  REQUIRE(AllWarnings(summary).find("shadowed") != std::string::npos);
  REQUIRE(AllWarnings(summary).find("env-secret") == std::string::npos);
  REQUIRE(AllWarnings(summary).find("config-secret") == std::string::npos);
  REQUIRE(AllWarnings(summary).find("access-secret") == std::string::npos);
  REQUIRE(AllWarnings(summary).find("refresh-secret") == std::string::npos);
}
