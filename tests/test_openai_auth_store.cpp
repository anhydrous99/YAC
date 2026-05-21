#include "provider/openai_auth.hpp"
#include "provider/openai_auth_store.hpp"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>
#endif

using Catch::Matchers::ContainsSubstring;

namespace yac::provider::test {
namespace {

using Json = nlohmann::json;

class TempDir {
 public:
  TempDir() {
#ifndef _WIN32
    std::string tmpl =
        (std::filesystem::temp_directory_path() / "yac_test_openai_auth_XXXXXX")
            .string();
    const char* result = ::mkdtemp(tmpl.data());
    if (result == nullptr) {
      throw std::runtime_error("TempDir: mkdtemp failed");
    }
    path_ = result;
#else
    path_ = std::filesystem::temp_directory_path() / "yac_test_openai_auth";
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

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
    if (const char* current = std::getenv(name_.c_str())) {
      previous_ = std::string(current);
    }
    setenv(name_.c_str(), value.c_str(), 1);
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

class MemoryAuthBackend : public IOpenAiAuthBackend {
 public:
  explicit MemoryAuthBackend(std::optional<std::string> initial = std::nullopt)
      : value_(std::move(initial)) {}

  [[nodiscard]] std::optional<std::string> Get() const override {
    return value_;
  }

  void Set(std::string_view auth_json) override {
    value_ = std::string(auth_json);
  }

  void Erase() override { value_.reset(); }

 private:
  std::optional<std::string> value_;
};

class ThrowingKeychainBackend : public IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
  }

  void Set(std::string_view auth_json) override {
    (void)auth_json;
    throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
  }

  void Erase() override {
    throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
  }
};

}  // namespace

TEST_CASE("api_key_serialization_round_trip", "[openai_auth_store]") {
  const OpenAiAuth auth = OpenAiApiKeyAuth{
      .key = "sk-test-12345678",
      .metadata = Json{{"label", "manual"}, {"createdBy", "test"}},
  };

  const std::string serialized = SerializeOpenAiAuth(auth);
  const OpenAiAuth parsed = ParseOpenAiAuth(serialized);

  REQUIRE(OpenAiAuthTypeLabel(parsed) == std::string_view("OpenAI API key"));
  const auto* api_key = std::get_if<OpenAiApiKeyAuth>(&parsed);
  REQUIRE(api_key != nullptr);
  REQUIRE(api_key->key == "sk-test-12345678");
  REQUIRE(api_key->metadata.has_value());
  REQUIRE((*api_key->metadata)["label"] == "manual");
}

TEST_CASE("oauth_serialization_round_trip", "[openai_auth_store]") {
  const auto expiry =
      std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}};
  const OpenAiAuth auth = OpenAiOAuthAuth{
      .refresh_token = "rt-test-refresh",
      .access_token = "at-test-access",
      .expires_at = expiry,
      .account_id = "acct_test",
      .enterprise_url = "https://example.openai.test",
  };

  const std::string serialized = SerializeOpenAiAuth(auth);
  const OpenAiAuth parsed = ParseOpenAiAuth(serialized);

  REQUIRE(OpenAiAuthTypeLabel(parsed) == std::string_view("OpenAI OAuth"));
  const auto* oauth = std::get_if<OpenAiOAuthAuth>(&parsed);
  REQUIRE(oauth != nullptr);
  REQUIRE(oauth->refresh_token == "rt-test-refresh");
  REQUIRE(oauth->access_token == "at-test-access");
  REQUIRE(oauth->expires_at == expiry);
  REQUIRE(oauth->account_id == std::optional<std::string>{"acct_test"});
  REQUIRE(oauth->enterprise_url ==
          std::optional<std::string>{"https://example.openai.test"});
}

TEST_CASE("missing_and_invalid_json_are_rejected", "[openai_auth_store]") {
  REQUIRE_THROWS(ParseOpenAiAuth("{"));
  REQUIRE_THROWS(ParseOpenAiAuth(R"({"type":"api"})"));
  REQUIRE_THROWS(
      ParseOpenAiAuth(R"({"type":"oauth","refresh":"rt","access":123})"));
  REQUIRE_THROWS(ParseOpenAiAuth(
      R"({"type":"oauth","refresh":"rt","access":"at","expires":"soon"})"));
}

TEST_CASE("expiry_helpers", "[openai_auth_store]") {
  const auto now =
      std::chrono::system_clock::time_point{std::chrono::seconds{200}};
  const OpenAiOAuthAuth valid{
      .refresh_token = "rt-test",
      .access_token = "at-test",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{300}}};
  const OpenAiOAuthAuth expired{
      .refresh_token = "rt-test",
      .access_token = "at-test",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{100}}};

  REQUIRE_FALSE(IsOpenAiOAuthExpired(valid, now));
  REQUIRE(HasUsableOpenAiOAuthAccessToken(valid, now));
  REQUIRE(IsOpenAiOAuthExpired(expired, now));
  REQUIRE_FALSE(HasUsableOpenAiOAuthAccessToken(expired, now));
}

TEST_CASE("redacted_display_hides_secrets", "[openai_auth_store]") {
  const OpenAiAuth api_auth = OpenAiApiKeyAuth{.key = "sk-test-secret-value"};
  const OpenAiAuth oauth_auth =
      OpenAiOAuthAuth{.refresh_token = "rt-test-secret-value",
                      .access_token = "at-test-secret-value"};

  const std::string api_description = DescribeOpenAiAuth(api_auth);
  const std::string oauth_description = DescribeOpenAiAuth(oauth_auth);

  REQUIRE(RedactOpenAiSecret("sk-test-secret-value") != "sk-test-secret-value");
  REQUIRE(api_description.find("sk-test-secret-value") == std::string::npos);
  REQUIRE(oauth_description.find("rt-test-secret-value") == std::string::npos);
  REQUIRE(oauth_description.find("at-test-secret-value") == std::string::npos);
}

TEST_CASE("keychain_fallback_uses_file_backend", "[openai_auth_store]") {
  auto keychain = std::make_shared<ThrowingKeychainBackend>();
  auto file_backend = std::make_shared<MemoryAuthBackend>();
  OpenAiAuthStore store(OpenAiAuthStore::Dependencies{
      .keychain_backend = keychain,
      .file_backend = file_backend,
  });
  const OpenAiAuth auth = OpenAiApiKeyAuth{.key = "sk-test-fallback"};

  const auto saved_source = store.Save(auth);
  const auto loaded = store.Load();

  REQUIRE(saved_source == OpenAiAuthStorageSource::File);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->source == OpenAiAuthStorageSource::File);
  REQUIRE(OpenAiAuthStorageSourceLabel(loaded->source) ==
          std::string_view("provider auth file"));
  const auto* api_key = std::get_if<OpenAiApiKeyAuth>(&loaded->auth);
  REQUIRE(api_key != nullptr);
  REQUIRE(api_key->key == "sk-test-fallback");
}

TEST_CASE("file_env_skips_keychain_backend", "[openai_auth_store]") {
  TempDir temp_dir;
  ScopedEnvVar home("HOME", temp_dir.Path().string());
  ScopedEnvVar auth_store("YAC_OPENAI_AUTH_STORE", "file");
  OpenAiAuthStore store;
  const OpenAiAuth auth = OpenAiApiKeyAuth{.key = "sk-test-file-only"};

  const auto saved_source = store.Save(auth);
  const auto loaded = store.Load();

  REQUIRE(saved_source == OpenAiAuthStorageSource::File);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->source == OpenAiAuthStorageSource::File);
  REQUIRE(std::filesystem::exists(temp_dir.Path() / ".yac" / "provider" /
                                  "auth" / "openai.json"));
}

TEST_CASE("file_backend_writes_mode_0600_and_rejects_broad_perms",
          "[openai_auth_store]") {
  TempDir temp_dir;
  const auto auth_path = temp_dir.Path() / "openai.json";
  OpenAiFileAuthBackend backend(auth_path);
  const OpenAiAuth auth = OpenAiApiKeyAuth{.key = "sk-test-mode-check"};

  backend.Set(SerializeOpenAiAuth(auth));

#ifndef _WIN32
  struct ::stat st{};
  REQUIRE(::stat(auth_path.c_str(), &st) == 0);
  REQUIRE((st.st_mode & 0777) == 0600);

  ::chmod(auth_path.c_str(), S_IRUSR | S_IWUSR | S_IRGRP);
  REQUIRE_THROWS_WITH(backend.Get(), ContainsSubstring("permissions"));
#else
  REQUIRE(backend.Get().has_value());
#endif
}

}  // namespace yac::provider::test
