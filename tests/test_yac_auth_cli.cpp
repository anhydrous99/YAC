#include "cli/provider_auth_cli_dispatch.hpp"

#include "provider/openai_auth_store.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>

#include <catch2/catch_test_macros.hpp>

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

class NotifyingStringBuf : public std::streambuf {
 public:
  [[nodiscard]] std::string Snapshot() const {
    std::scoped_lock lock(mutex_);
    return buffer_;
  }

  [[nodiscard]] bool WaitForSubstring(std::string_view needle,
                                      std::chrono::milliseconds timeout) const {
    std::unique_lock lock(mutex_);
    return cv_.wait_for(lock, timeout, [&] {
      return buffer_.find(needle) != std::string::npos;
    });
  }

 protected:
  std::streamsize xsputn(const char* s, std::streamsize count) override {
    {
      std::scoped_lock lock(mutex_);
      buffer_.append(s, static_cast<std::size_t>(count));
    }
    cv_.notify_all();
    return count;
  }

  int overflow(int ch) override {
    if (ch == traits_type::eof()) {
      return ch;
    }
    {
      std::scoped_lock lock(mutex_);
      buffer_.push_back(static_cast<char>(ch));
    }
    cv_.notify_all();
    return ch;
  }

 private:
  mutable std::mutex mutex_;
  mutable std::condition_variable cv_;
  std::string buffer_;
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

std::vector<char*> Argv(std::vector<std::string>& args) {
  std::vector<char*> argv;
  argv.reserve(args.size());
  for (auto& arg : args) {
    argv.push_back(arg.data());
  }
  return argv;
}

}  // namespace

TEST_CASE("auth cli prints usage with no args", "[yac_auth_cli]") {
  std::ostringstream out;
  std::ostringstream err;

  yac::cli::ProviderAuthCliOptions opts;
  opts.out = &out;
  opts.err = &err;

  const int rc = yac::cli::RunProviderAuthCli(0, nullptr, std::move(opts));

  REQUIRE(rc == 0);
  REQUIRE(out.str().find("Usage: yac auth") != std::string::npos);
  REQUIRE(err.str().empty());
}

TEST_CASE("auth cli rejects unknown provider", "[yac_auth_cli]") {
  std::ostringstream out;
  std::ostringstream err;
  std::vector<std::string> args = {"other", "status"};
  auto argv = Argv(args);

  yac::cli::ProviderAuthCliOptions opts;
  opts.out = &out;
  opts.err = &err;

  const int rc =
      yac::cli::RunProviderAuthCli(static_cast<int>(argv.size()), argv.data(),
                                   std::move(opts));

  REQUIRE(rc == 1);
  REQUIRE(err.str().find("unknown provider") != std::string::npos);
}

TEST_CASE("auth cli rejects unknown subcommand", "[yac_auth_cli]") {
  std::ostringstream out;
  std::ostringstream err;
  std::vector<std::string> args = {"openai", "mystery"};
  auto argv = Argv(args);

  yac::cli::ProviderAuthCliOptions opts;
  opts.out = &out;
  opts.err = &err;

  const int rc =
      yac::cli::RunProviderAuthCli(static_cast<int>(argv.size()), argv.data(),
                                   std::move(opts));

  REQUIRE(rc == 1);
  REQUIRE(err.str().find("unknown subcommand") != std::string::npos);
}

TEST_CASE("auth cli set-api-key stores stdin secret without echoing",
          "[yac_auth_cli]") {
  TempDir tmp("yac_test_auth_cli_set_api_key");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path, "[provider]\nid = \"openai-compatible\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  std::istringstream input("sk-test\n");
  std::ostringstream out;
  std::ostringstream err;
  std::vector<std::string> args = {"openai", "set-api-key", "--stdin"};
  auto argv = Argv(args);

  yac::cli::ProviderAuthCliOptions opts;
  opts.command_options.settings_path = settings_path;
  opts.command_options.auth_store = store;
  opts.command_options.in = &input;
  opts.out = &out;
  opts.err = &err;

  const int rc =
      yac::cli::RunProviderAuthCli(static_cast<int>(argv.size()), argv.data(),
                                   std::move(opts));

  REQUIRE(rc == 0);
  REQUIRE(err.str().empty());
  REQUIRE(out.str().find("Stored OpenAI API key.") != std::string::npos);
  REQUIRE(out.str().find("sk-test") == std::string::npos);
  const auto stored = store->Load();
  REQUIRE(stored.has_value());
  REQUIRE(std::holds_alternative<yac::provider::OpenAiApiKeyAuth>(stored->auth));
}

TEST_CASE("auth cli status redacts stored oauth secrets", "[yac_auth_cli]") {
  TempDir tmp("yac_test_auth_cli_status");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "api_key_env = \"OPENAI_API_KEY\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  static_cast<void>(store->Save(yac::provider::OpenAiOAuthAuth{
      .refresh_token = "refresh-secret",
      .access_token = "access-secret",
      .expires_at = std::chrono::system_clock::time_point{
          std::chrono::seconds{4102444800}},
      .account_id = std::string("acct-safe"),
  }));
  std::ostringstream out;
  std::ostringstream err;
  std::vector<std::string> args = {"openai", "status"};
  auto argv = Argv(args);

  yac::cli::ProviderAuthCliOptions opts;
  opts.command_options.settings_path = settings_path;
  opts.command_options.auth_store = store;
  opts.out = &out;
  opts.err = &err;

  const int rc =
      yac::cli::RunProviderAuthCli(static_cast<int>(argv.size()), argv.data(),
                                   std::move(opts));

  REQUIRE(rc == 0);
  REQUIRE(err.str().empty());
  REQUIRE(out.str().find("configured provider:") != std::string::npos);
  REQUIRE(out.str().find("stored credential: oauth") != std::string::npos);
  REQUIRE(out.str().find("effective auth: oauth (stored)") != std::string::npos);
  REQUIRE(out.str().find("oauth expiry: 4102444800") != std::string::npos);
  REQUIRE(out.str().find("account id: acct-safe") != std::string::npos);
  REQUIRE(out.str().find("access-secret") == std::string::npos);
  REQUIRE(out.str().find("refresh-secret") == std::string::npos);
}

TEST_CASE("auth cli logout clears stored openai auth", "[yac_auth_cli]") {
  TempDir tmp("yac_test_auth_cli_logout");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path, "[provider]\nid = \"openai-compatible\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  static_cast<void>(store->Save(yac::provider::OpenAiApiKeyAuth{.key = "sk-test"}));
  std::ostringstream out;
  std::ostringstream err;
  std::vector<std::string> args = {"openai", "logout"};
  auto argv = Argv(args);

  yac::cli::ProviderAuthCliOptions opts;
  opts.command_options.settings_path = settings_path;
  opts.command_options.auth_store = store;
  opts.out = &out;
  opts.err = &err;

  const int rc =
      yac::cli::RunProviderAuthCli(static_cast<int>(argv.size()), argv.data(),
                                   std::move(opts));

  REQUIRE(rc == 0);
  REQUIRE(out.str().find("Logged out: openai") != std::string::npos);
  REQUIRE_FALSE(store->Load().has_value());
}

TEST_CASE("auth cli prints browser fallback URL before oauth completion",
          "[yac_auth_cli]") {
  TempDir tmp("yac_test_auth_cli_login_fallback");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path, "[provider]\nid = \"openai\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  auto release = std::make_shared<std::atomic<bool>>(false);
  std::vector<std::string> args = {"openai", "login"};
  auto argv = Argv(args);
  NotifyingStringBuf out_buf;
  std::ostream out(&out_buf);
  std::ostringstream err;

  yac::cli::ProviderAuthCliOptions opts;
  opts.command_options.settings_path = settings_path;
  opts.command_options.auth_store = store;
  opts.command_options.login_fn = [release](
                                     const yac::provider::OpenAiAuthorizationObserver& observer)
      -> yac::provider::OpenAiOAuthAuth {
    observer(yac::provider::OpenAiAuthorizationNotice{
        .authorization_url = "https://auth.openai.com/oauth/authorize?state=manual",
        .redirect_uri = "http://127.0.0.1:1455/auth/callback",
        .browser_launched = false,
    });
    while (!release->load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return yac::provider::OpenAiOAuthAuth{
        .refresh_token = "refresh",
        .access_token = "access",
    };
  };
  opts.out = &out;
  opts.err = &err;

  auto worker = std::async(std::launch::async, [&] {
    return yac::cli::RunProviderAuthCli(static_cast<int>(argv.size()), argv.data(),
                                        std::move(opts));
  });

  REQUIRE(out_buf.WaitForSubstring(
      "warning: browser launch failed; open this URL manually: https://auth.openai.com/oauth/authorize?state=manual",
      std::chrono::milliseconds(500)));
  REQUIRE(worker.wait_for(std::chrono::milliseconds(0)) ==
          std::future_status::timeout);
  REQUIRE_FALSE(store->Load().has_value());

  release->store(true);
  REQUIRE(worker.get() == 0);
  REQUIRE(err.str().empty());
  const std::string output = out_buf.Snapshot();
  REQUIRE(output.find("Authenticated successfully.") != std::string::npos);
}

TEST_CASE("auth cli logout warns when env auth remains effective",
          "[yac_auth_cli]") {
  TempDir tmp("yac_test_auth_cli_logout_warn");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai-compatible\"\n"
            "api_key_env = \"OPENAI_API_KEY\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  static_cast<void>(store->Save(yac::provider::OpenAiApiKeyAuth{.key = "sk-test"}));
  ScopedEnvVar env("OPENAI_API_KEY", "env-secret");
  std::ostringstream out;
  std::ostringstream err;
  std::vector<std::string> args = {"openai", "logout"};
  auto argv = Argv(args);

  yac::cli::ProviderAuthCliOptions opts;
  opts.command_options.settings_path = settings_path;
  opts.command_options.auth_store = store;
  opts.out = &out;
  opts.err = &err;

  const int rc =
      yac::cli::RunProviderAuthCli(static_cast<int>(argv.size()), argv.data(),
                                   std::move(opts));

  REQUIRE(rc == 0);
  REQUIRE(out.str().find("warning: OPENAI_API_KEY remains effective after logout") !=
          std::string::npos);
  REQUIRE(out.str().find("env-secret") == std::string::npos);
}

TEST_CASE("auth cli rejects openai --no-browser login flag", "[yac_auth_cli]") {
  TempDir tmp("yac_test_auth_cli_no_browser");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path, "[provider]\nid = \"openai-compatible\"\n");

  bool login_called = false;
  std::ostringstream out;
  std::ostringstream err;
  std::vector<std::string> args = {"openai", "login", "--no-browser"};
  auto argv = Argv(args);

  yac::cli::ProviderAuthCliOptions opts;
  opts.command_options.settings_path = settings_path;
  opts.command_options.auth_store = MakeStore(std::make_shared<MemoryAuthBackend>());
  opts.command_options.login_fn =
      [&login_called](const yac::provider::OpenAiAuthorizationObserver&) {
        login_called = true;
        return yac::provider::OpenAiOAuthAuth{};
      };
  opts.out = &out;
  opts.err = &err;

  const int rc =
      yac::cli::RunProviderAuthCli(static_cast<int>(argv.size()), argv.data(),
                                   std::move(opts));

  REQUIRE(rc == 1);
  REQUIRE(err.str().find("unknown flag: --no-browser") != std::string::npos);
  REQUIRE_FALSE(login_called);
}
