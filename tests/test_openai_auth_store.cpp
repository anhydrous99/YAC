#include "provider/openai_auth.hpp"
#include "provider/openai_auth_store.hpp"

#include <array>
#include <barrier>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
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

class CountingAuthBackend : public IOpenAiAuthBackend {
 public:
  explicit CountingAuthBackend(
      std::optional<std::string> initial = std::nullopt)
      : value_(std::move(initial)) {}

  [[nodiscard]] std::optional<std::string> Get() const override {
    std::scoped_lock lock(mutex_);
    ++get_count_;
    if (keychain_unavailable_on_get_) {
      throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
    }
    if (throw_on_get_) {
      throw std::runtime_error("backend read failed");
    }
    return value_;
  }

  void Set(std::string_view auth_json) override {
    std::scoped_lock lock(mutex_);
    ++set_count_;
    if (keychain_unavailable_on_set_) {
      throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
    }
    if (throw_on_set_) {
      throw std::runtime_error("backend write failed");
    }
    value_ = std::string(auth_json);
  }

  void Erase() override {
    std::scoped_lock lock(mutex_);
    ++erase_count_;
    if (keychain_unavailable_on_erase_) {
      throw OpenAiAuthKeychainUnavailableError("keychain unavailable");
    }
    if (throw_on_erase_) {
      throw std::runtime_error("backend erase failed");
    }
    value_.reset();
  }

  void SetValue(std::optional<std::string> value) {
    std::scoped_lock lock(mutex_);
    value_ = std::move(value);
  }

  void SetKeychainUnavailable(bool unavailable) {
    std::scoped_lock lock(mutex_);
    keychain_unavailable_on_get_ = unavailable;
    keychain_unavailable_on_set_ = unavailable;
    keychain_unavailable_on_erase_ = unavailable;
  }

  void SetKeychainUnavailableOnGet(bool unavailable) {
    std::scoped_lock lock(mutex_);
    keychain_unavailable_on_get_ = unavailable;
  }

  void SetKeychainUnavailableOnSet(bool unavailable) {
    std::scoped_lock lock(mutex_);
    keychain_unavailable_on_set_ = unavailable;
  }

  void SetKeychainUnavailableOnErase(bool unavailable) {
    std::scoped_lock lock(mutex_);
    keychain_unavailable_on_erase_ = unavailable;
  }

  void SetThrowOnGet(bool throw_on_get) {
    std::scoped_lock lock(mutex_);
    throw_on_get_ = throw_on_get;
  }

  void SetThrowOnSet(bool throw_on_set) {
    std::scoped_lock lock(mutex_);
    throw_on_set_ = throw_on_set;
  }

  void SetThrowOnErase(bool throw_on_erase) {
    std::scoped_lock lock(mutex_);
    throw_on_erase_ = throw_on_erase;
  }

  [[nodiscard]] int GetCount() const {
    std::scoped_lock lock(mutex_);
    return get_count_;
  }

  [[nodiscard]] int SetCount() const {
    std::scoped_lock lock(mutex_);
    return set_count_;
  }

  [[nodiscard]] int EraseCount() const {
    std::scoped_lock lock(mutex_);
    return erase_count_;
  }

 private:
  mutable std::mutex mutex_;
  std::optional<std::string> value_;
  mutable int get_count_ = 0;
  int set_count_ = 0;
  int erase_count_ = 0;
  bool keychain_unavailable_on_get_ = false;
  bool keychain_unavailable_on_set_ = false;
  bool keychain_unavailable_on_erase_ = false;
  bool throw_on_get_ = false;
  bool throw_on_set_ = false;
  bool throw_on_erase_ = false;
};

[[nodiscard]] std::string SerializedDummyApiKeyAuth(
    std::string key = "dummy-api-key") {
  return SerializeOpenAiAuth(OpenAiApiKeyAuth{.key = std::move(key)});
}

[[nodiscard]] std::shared_ptr<OpenAiAuthStore> MakeCountingStore(
    const std::shared_ptr<CountingAuthBackend>& keychain,
    const std::shared_ptr<CountingAuthBackend>& file_backend) {
  return std::make_shared<OpenAiAuthStore>(OpenAiAuthStore::Dependencies{
      .keychain_backend = keychain,
      .file_backend = file_backend,
  });
}

[[nodiscard]] const OpenAiApiKeyAuth* RequireApiKeyAuth(
    const std::optional<StoredOpenAiAuth>& stored) {
  REQUIRE(stored.has_value());
  const auto* api_key = std::get_if<OpenAiApiKeyAuth>(&stored->auth);
  REQUIRE(api_key != nullptr);
  return api_key;
}

}  // namespace

TEST_CASE("api_key_serialization_round_trip", "[openai_auth_store]") {
  const OpenAiAuth auth = OpenAiApiKeyAuth{
      .key = "dummy-api-key-12345678",
      .metadata = Json{{"label", "manual"}, {"createdBy", "test"}},
  };

  const std::string serialized = SerializeOpenAiAuth(auth);
  const OpenAiAuth parsed = ParseOpenAiAuth(serialized);

  REQUIRE(OpenAiAuthTypeLabel(parsed) == std::string_view("OpenAI API key"));
  const auto* api_key = std::get_if<OpenAiApiKeyAuth>(&parsed);
  REQUIRE(api_key != nullptr);
  REQUIRE(api_key->key == "dummy-api-key-12345678");
  REQUIRE(api_key->metadata.has_value());
  REQUIRE((*api_key->metadata)["label"] == "manual");
}

TEST_CASE("oauth_serialization_round_trip", "[openai_auth_store]") {
  const auto expiry =
      std::chrono::system_clock::time_point{std::chrono::seconds{1700000000}};
  const OpenAiAuth auth = OpenAiOAuthAuth{
      .refresh_token = "dummy-refresh-value",
      .access_token = "dummy-access-value",
      .expires_at = expiry,
      .account_id = "acct_test",
      .enterprise_url = "https://example.openai.test",
  };

  const std::string serialized = SerializeOpenAiAuth(auth);
  const OpenAiAuth parsed = ParseOpenAiAuth(serialized);

  REQUIRE(OpenAiAuthTypeLabel(parsed) == std::string_view("OpenAI OAuth"));
  const auto* oauth = std::get_if<OpenAiOAuthAuth>(&parsed);
  REQUIRE(oauth != nullptr);
  REQUIRE(oauth->refresh_token == "dummy-refresh-value");
  REQUIRE(oauth->access_token == "dummy-access-value");
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
      .refresh_token = "dummy-refresh",
      .access_token = "dummy-access",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{300}}};
  const OpenAiOAuthAuth expired{
      .refresh_token = "dummy-refresh",
      .access_token = "dummy-access",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{100}}};

  REQUIRE_FALSE(IsOpenAiOAuthExpired(valid, now));
  REQUIRE(HasUsableOpenAiOAuthAccessToken(valid, now));
  REQUIRE(IsOpenAiOAuthExpired(expired, now));
  REQUIRE_FALSE(HasUsableOpenAiOAuthAccessToken(expired, now));
}

TEST_CASE("redacted_display_hides_credentials", "[openai_auth_store]") {
  const OpenAiAuth api_auth =
      OpenAiApiKeyAuth{.key = "dummy-api-key-private-value"};
  const OpenAiAuth oauth_auth =
      OpenAiOAuthAuth{.refresh_token = "dummy-refresh-private-value",
                      .access_token = "dummy-access-private-value"};

  const std::string api_description = DescribeOpenAiAuth(api_auth);
  const std::string oauth_description = DescribeOpenAiAuth(oauth_auth);

  REQUIRE(RedactOpenAiSecret("dummy-api-key-private-value") !=
          "dummy-api-key-private-value");
  REQUIRE(api_description.find("dummy-api-key-private-value") ==
          std::string::npos);
  REQUIRE(oauth_description.find("dummy-refresh-private-value") ==
          std::string::npos);
  REQUIRE(oauth_description.find("dummy-access-private-value") ==
          std::string::npos);
}

TEST_CASE("keychain_fallback_uses_file_backend", "[openai_auth_store]") {
  auto keychain = std::make_shared<ThrowingKeychainBackend>();
  auto file_backend = std::make_shared<MemoryAuthBackend>();
  OpenAiAuthStore store(OpenAiAuthStore::Dependencies{
      .keychain_backend = keychain,
      .file_backend = file_backend,
  });
  const OpenAiAuth auth = OpenAiApiKeyAuth{.key = "dummy-fallback-key"};

  const auto saved_source = store.Save(auth);
  const auto loaded = store.Load();

  REQUIRE(saved_source == OpenAiAuthStorageSource::File);
  REQUIRE(loaded.has_value());
  REQUIRE(loaded->source == OpenAiAuthStorageSource::File);
  REQUIRE(OpenAiAuthStorageSourceLabel(loaded->source) ==
          std::string_view("provider auth file"));
  const auto* api_key = std::get_if<OpenAiApiKeyAuth>(&loaded->auth);
  REQUIRE(api_key != nullptr);
  REQUIRE(api_key->key == "dummy-fallback-key");
}

TEST_CASE("stored_auth_present_is_memoized", "[openai_auth_store]") {
  auto keychain = std::make_shared<CountingAuthBackend>(
      SerializedDummyApiKeyAuth("stored-api-key-cache"));
  auto file_backend = std::make_shared<CountingAuthBackend>();
  const auto store = MakeCountingStore(keychain, file_backend);

  const auto first = store->Load();
  const auto second = store->Load();

  REQUIRE(first->source == OpenAiAuthStorageSource::Keychain);
  REQUIRE(second->source == OpenAiAuthStorageSource::Keychain);
  REQUIRE(RequireApiKeyAuth(first)->key == "stored-api-key-cache");
  REQUIRE(RequireApiKeyAuth(second)->key == "stored-api-key-cache");
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 0);
}

TEST_CASE("concurrent_loads_share_one_backend_read", "[openai_auth_store]") {
  constexpr std::size_t kCallerCount = 8;
  constexpr std::string_view kStoredKey = "stored-api-key-concurrent-cache";
  auto keychain = std::make_shared<CountingAuthBackend>(
      SerializedDummyApiKeyAuth(std::string(kStoredKey)));
  auto file_backend = std::make_shared<CountingAuthBackend>();
  const auto store = MakeCountingStore(keychain, file_backend);
  std::barrier start_barrier(kCallerCount);
  std::array<std::optional<StoredOpenAiAuth>, kCallerCount> results;
  std::array<std::thread, kCallerCount> callers;

  for (std::size_t index = 0; index < kCallerCount; ++index) {
    callers[index] = std::thread([&, index] {
      start_barrier.arrive_and_wait();
      results[index] = store->Load();
    });
  }

  for (auto& caller : callers) {
    caller.join();
  }

  for (const auto& loaded : results) {
    REQUIRE(loaded.has_value());
    REQUIRE(loaded->source == OpenAiAuthStorageSource::Keychain);
    REQUIRE(RequireApiKeyAuth(loaded)->key == kStoredKey);
  }
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 0);
}

TEST_CASE("keychain_unavailable_file_auth_is_memoized", "[openai_auth_store]") {
  auto keychain = std::make_shared<CountingAuthBackend>();
  keychain->SetKeychainUnavailable(true);
  auto file_backend = std::make_shared<CountingAuthBackend>(
      SerializedDummyApiKeyAuth("stored-file-api-key-cache"));
  const auto store = MakeCountingStore(keychain, file_backend);

  const auto first = store->Load();
  const auto second = store->Load();

  REQUIRE(first->source == OpenAiAuthStorageSource::File);
  REQUIRE(second->source == OpenAiAuthStorageSource::File);
  REQUIRE(RequireApiKeyAuth(first)->key == "stored-file-api-key-cache");
  REQUIRE(RequireApiKeyAuth(second)->key == "stored-file-api-key-cache");
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 1);
}

TEST_CASE("missing_auth_is_memoized", "[openai_auth_store]") {
  auto keychain = std::make_shared<CountingAuthBackend>();
  auto file_backend = std::make_shared<CountingAuthBackend>();
  const auto store = MakeCountingStore(keychain, file_backend);

  REQUIRE_FALSE(store->Load().has_value());
  REQUIRE_FALSE(store->Load().has_value());
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 1);
}

TEST_CASE("keychain_unavailable_missing_auth_is_memoized",
          "[openai_auth_store]") {
  auto keychain = std::make_shared<CountingAuthBackend>();
  keychain->SetKeychainUnavailable(true);
  auto file_backend = std::make_shared<CountingAuthBackend>();
  const auto store = MakeCountingStore(keychain, file_backend);

  REQUIRE_FALSE(store->Load().has_value());
  REQUIRE_FALSE(store->Load().has_value());
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 1);
}

TEST_CASE("save_updates_load_cache", "[openai_auth_store]") {
  SECTION("keychain save") {
    auto keychain = std::make_shared<CountingAuthBackend>();
    auto file_backend = std::make_shared<CountingAuthBackend>();
    const auto store = MakeCountingStore(keychain, file_backend);

    REQUIRE(store->Save(OpenAiApiKeyAuth{.key = "dummy-save-keychain"}) ==
            OpenAiAuthStorageSource::Keychain);
    const auto loaded = store->Load();

    REQUIRE(loaded->source == OpenAiAuthStorageSource::Keychain);
    REQUIRE(RequireApiKeyAuth(loaded)->key == "dummy-save-keychain");
    REQUIRE(keychain->SetCount() == 1);
    REQUIRE(keychain->GetCount() == 0);
    REQUIRE(file_backend->GetCount() == 0);
  }

  SECTION("file fallback save") {
    auto keychain = std::make_shared<CountingAuthBackend>();
    keychain->SetKeychainUnavailable(true);
    auto file_backend = std::make_shared<CountingAuthBackend>();
    const auto store = MakeCountingStore(keychain, file_backend);

    REQUIRE(store->Save(OpenAiApiKeyAuth{.key = "dummy-save-file"}) ==
            OpenAiAuthStorageSource::File);
    const auto loaded = store->Load();

    REQUIRE(loaded->source == OpenAiAuthStorageSource::File);
    REQUIRE(RequireApiKeyAuth(loaded)->key == "dummy-save-file");
    REQUIRE(keychain->SetCount() == 1);
    REQUIRE(file_backend->SetCount() == 1);
    REQUIRE(keychain->GetCount() == 0);
    REQUIRE(file_backend->GetCount() == 0);
  }
}

TEST_CASE("save_updates_cached_auth_without_reread", "[openai_auth_store]") {
  auto keychain = std::make_shared<CountingAuthBackend>(
      SerializedDummyApiKeyAuth("dummy-old-save-cache"));
  auto file_backend = std::make_shared<CountingAuthBackend>();
  const auto store = MakeCountingStore(keychain, file_backend);

  const auto old_auth = store->Load();
  const int get_count_after_initial_load = keychain->GetCount();
  const auto saved_source =
      store->Save(OpenAiApiKeyAuth{.key = "dummy-new-save-cache"});
  const auto loaded_after_save = store->Load();

  REQUIRE(saved_source == OpenAiAuthStorageSource::Keychain);
  REQUIRE(old_auth->source == OpenAiAuthStorageSource::Keychain);
  REQUIRE(RequireApiKeyAuth(old_auth)->key == "dummy-old-save-cache");
  REQUIRE(loaded_after_save->source == OpenAiAuthStorageSource::Keychain);
  REQUIRE(RequireApiKeyAuth(loaded_after_save)->key == "dummy-new-save-cache");
  REQUIRE(keychain->GetCount() == get_count_after_initial_load);
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 0);
  REQUIRE(keychain->SetCount() == 1);
  REQUIRE(file_backend->SetCount() == 0);
}

TEST_CASE("erase_updates_load_cache", "[openai_auth_store]") {
  const OpenAiAuth auth = OpenAiApiKeyAuth{.key = "dummy-erase-cache"};
  auto keychain =
      std::make_shared<CountingAuthBackend>(SerializeOpenAiAuth(auth));
  auto file_backend = std::make_shared<CountingAuthBackend>();
  const auto store = MakeCountingStore(keychain, file_backend);

  REQUIRE(store->Load().has_value());
  store->Erase();

  REQUIRE_FALSE(store->Load().has_value());
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 0);
  REQUIRE(keychain->EraseCount() == 1);
  REQUIRE(file_backend->EraseCount() == 1);
}

TEST_CASE("erase_clears_cached_auth_without_reread", "[openai_auth_store]") {
  auto keychain = std::make_shared<CountingAuthBackend>(
      SerializedDummyApiKeyAuth("dummy-old-erase-cache"));
  auto file_backend = std::make_shared<CountingAuthBackend>();
  const auto store = MakeCountingStore(keychain, file_backend);

  const auto old_auth = store->Load();
  const int get_count_after_initial_load = keychain->GetCount();
  store->Erase();
  const auto loaded_after_erase = store->Load();

  REQUIRE(old_auth->source == OpenAiAuthStorageSource::Keychain);
  REQUIRE(RequireApiKeyAuth(old_auth)->key == "dummy-old-erase-cache");
  REQUIRE_FALSE(loaded_after_erase.has_value());
  REQUIRE(keychain->GetCount() == get_count_after_initial_load);
  REQUIRE(keychain->GetCount() == 1);
  REQUIRE(file_backend->GetCount() == 0);
  REQUIRE(keychain->EraseCount() == 1);
  REQUIRE(file_backend->EraseCount() == 1);
}

TEST_CASE("load_failures_are_not_cached", "[openai_auth_store]") {
  SECTION("parse error retries") {
    auto keychain = std::make_shared<CountingAuthBackend>();
    auto file_backend = std::make_shared<CountingAuthBackend>("{");
    const auto store = MakeCountingStore(keychain, file_backend);

    REQUIRE_THROWS(store->Load());
    REQUIRE_THROWS(store->Load());
    REQUIRE(keychain->GetCount() == 2);
    REQUIRE(file_backend->GetCount() == 2);
  }

  SECTION("file read error retries") {
    auto keychain = std::make_shared<CountingAuthBackend>();
    auto file_backend = std::make_shared<CountingAuthBackend>();
    file_backend->SetThrowOnGet(true);
    const auto store = MakeCountingStore(keychain, file_backend);

    REQUIRE_THROWS_WITH(store->Load(),
                        ContainsSubstring("backend read failed"));
    REQUIRE_THROWS_WITH(store->Load(),
                        ContainsSubstring("backend read failed"));
    REQUIRE(keychain->GetCount() == 2);
    REQUIRE(file_backend->GetCount() == 2);
  }
}

TEST_CASE("file_env_skips_keychain_backend", "[openai_auth_store]") {
  TempDir temp_dir;
  ScopedEnvVar home("HOME", temp_dir.Path().string());
  ScopedEnvVar auth_store("YAC_OPENAI_AUTH_STORE", "file");
  OpenAiAuthStore store;
  const OpenAiAuth auth = OpenAiApiKeyAuth{.key = "dummy-file-only"};

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
  const OpenAiAuth auth = OpenAiApiKeyAuth{.key = "dummy-mode-check"};

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
