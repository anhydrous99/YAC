#include "app/provider_auth_command_handlers.hpp"
#include "cli/provider_auth_command.hpp"
#include "presentation/chat_ui.hpp"
#include "presentation/slash_command_registry.hpp"
#include "provider/openai_auth_store.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/app.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/loop.hpp>
#include <ftxui/dom/elements.hpp>

namespace {

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

std::shared_ptr<yac::cli::ProviderAuthCommand> MakeCommand(
    const std::filesystem::path& settings_path,
    const std::shared_ptr<yac::provider::OpenAiAuthStore>& store) {
  yac::cli::ProviderAuthCommand::Options opts;
  opts.settings_path = settings_path;
  opts.auth_store = store;
  return std::make_shared<yac::cli::ProviderAuthCommand>(std::move(opts));
}

}  // namespace

TEST_CASE("provider auth slash status matches CLI summary labels",
          "[slash_provider_auth]") {
  TempDir tmp("yac_test_slash_provider_auth_status");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai\"\n"
            "api_key_env = \"OPENAI_API_KEY\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  static_cast<void>(store->Save(yac::provider::OpenAiOAuthAuth{
      .refresh_token = "refresh-secret",
      .access_token = "access-secret",
      .expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{
          4102444800}},
      .account_id = std::string("acct-safe"),
  }));

  yac::presentation::ChatUI chat_ui;
  yac::presentation::SlashCommandRegistry registry;
  auto screen = ftxui::App::Fullscreen();
  yac::app::RegisterProviderAuthSlashCommandHandlers(
      registry, chat_ui, screen, MakeCommand(settings_path, store));

  REQUIRE(registry.TryDispatch("/auth openai status"));
  REQUIRE(chat_ui.GetMessages().size() == 1);
  const std::string text = chat_ui.GetMessages().front().CombinedText();
  REQUIRE(text.find("configured provider: openai") != std::string::npos);
  REQUIRE(text.find("stored credential: oauth") != std::string::npos);
  REQUIRE(text.find("effective auth: oauth (stored)") != std::string::npos);
  REQUIRE(text.find("oauth expiry: 4102444800") != std::string::npos);
  REQUIRE(text.find("account id: acct-safe") != std::string::npos);
  REQUIRE(text.find("access-secret") == std::string::npos);
  REQUIRE(text.find("refresh-secret") == std::string::npos);
}

TEST_CASE("provider auth slash logout clears stored auth",
          "[slash_provider_auth]") {
  TempDir tmp("yac_test_slash_provider_auth_logout");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  static_cast<void>(store->Save(yac::provider::OpenAiApiKeyAuth{.key = "sk"}));

  yac::presentation::ChatUI chat_ui;
  yac::presentation::SlashCommandRegistry registry;
  auto screen = ftxui::App::Fullscreen();
  yac::app::RegisterProviderAuthSlashCommandHandlers(
      registry, chat_ui, screen, MakeCommand(settings_path, store));

  REQUIRE(registry.TryDispatch("/auth openai logout"));
  REQUIRE_FALSE(store->Load().has_value());
  REQUIRE(chat_ui.GetNotices().size() == 1);
  REQUIRE(chat_ui.GetNotices().front().notice.title == "Logged out: openai");
}

TEST_CASE("provider auth slash set-api-key refuses inline secrets",
          "[slash_provider_auth]") {
  yac::presentation::ChatUI chat_ui;
  yac::presentation::SlashCommandRegistry registry;
  auto screen = ftxui::App::Fullscreen();
  yac::app::RegisterProviderAuthSlashCommandHandlers(
      registry, chat_ui, screen,
      std::make_shared<yac::cli::ProviderAuthCommand>());

  REQUIRE(registry.TryDispatch("/auth openai set-api-key sk-test"));
  REQUIRE(chat_ui.GetNotices().size() == 1);
  REQUIRE(chat_ui.GetNotices().front().notice.title ==
          "Use OPENAI_API_KEY or run: yac auth openai set-api-key --stdin");
  REQUIRE(chat_ui.GetNotices().front().notice.detail.empty());
}

TEST_CASE("provider auth slash rejects unknown provider",
          "[slash_provider_auth]") {
  yac::presentation::ChatUI chat_ui;
  yac::presentation::SlashCommandRegistry registry;
  auto screen = ftxui::App::Fullscreen();
  yac::app::RegisterProviderAuthSlashCommandHandlers(
      registry, chat_ui, screen,
      std::make_shared<yac::cli::ProviderAuthCommand>());

  REQUIRE(registry.TryDispatch("/auth other status"));
  REQUIRE(chat_ui.GetNotices().size() == 1);
  REQUIRE(chat_ui.GetNotices().front().notice.title ==
          "Unknown /auth provider: other");
}

TEST_CASE("provider auth slash rejects unknown openai subcommand",
          "[slash_provider_auth]") {
  yac::presentation::ChatUI chat_ui;
  yac::presentation::SlashCommandRegistry registry;
  auto screen = ftxui::App::Fullscreen();
  yac::app::RegisterProviderAuthSlashCommandHandlers(
      registry, chat_ui, screen,
      std::make_shared<yac::cli::ProviderAuthCommand>());

  REQUIRE(registry.TryDispatch("/auth openai mystery"));
  REQUIRE(chat_ui.GetNotices().size() == 1);
  REQUIRE(chat_ui.GetNotices().front().notice.title ==
          "Unknown /auth openai subcommand: mystery");
}

TEST_CASE("provider auth slash login dispatch is non-blocking",
          "[slash_provider_auth]") {
  TempDir tmp("yac_test_slash_provider_auth_login");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  auto entered = std::make_shared<std::atomic<bool>>(false);
  auto release = std::make_shared<std::atomic<bool>>(false);

  yac::cli::ProviderAuthCommand::Options opts;
  opts.settings_path = settings_path;
  opts.auth_store = store;
  opts.login_fn =
      [entered,
       release](const yac::provider::OpenAiAuthorizationObserver& observer)
      -> yac::provider::OpenAiOAuthAuth {
    observer(
        yac::provider::OpenAiAuthorizationNotice{.browser_launched = true});
    entered->store(true);
    while (!release->load()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return yac::provider::OpenAiOAuthAuth{
        .refresh_token = "refresh",
        .access_token = "access",
    };
  };

  yac::presentation::ChatUI chat_ui;
  yac::presentation::SlashCommandRegistry registry;
  auto screen = ftxui::App::Fullscreen();
  yac::app::RegisterProviderAuthSlashCommandHandlers(
      registry, chat_ui, screen,
      std::make_shared<yac::cli::ProviderAuthCommand>(std::move(opts)));

  const auto start = std::chrono::steady_clock::now();
  REQUIRE(registry.TryDispatch("/auth openai login"));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  REQUIRE(elapsed < 25);
  REQUIRE(chat_ui.GetNotices().size() == 1);
  REQUIRE(chat_ui.GetNotices().front().notice.title ==
          "Starting OpenAI auth...");
  REQUIRE_FALSE(entered->load());

  for (int attempt = 0; attempt < 100 && !entered->load(); ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(entered->load());

  release->store(true);
  for (int attempt = 0; attempt < 100 && !store->Load().has_value();
       ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(store->Load().has_value());
}

TEST_CASE(
    "provider auth slash shows browser fallback URL before oauth completion",
    "[slash_provider_auth]") {
  TempDir tmp("yac_test_slash_provider_auth_login_fallback");
  const auto settings_path = tmp.Path() / "settings.toml";
  WriteFile(settings_path,
            "[provider]\n"
            "id = \"openai\"\n");

  const auto backend = std::make_shared<MemoryAuthBackend>();
  const auto store = MakeStore(backend);
  auto release = std::make_shared<std::atomic<bool>>(false);

  yac::cli::ProviderAuthCommand::Options opts;
  opts.settings_path = settings_path;
  opts.auth_store = store;
  opts.login_fn =
      [release](const yac::provider::OpenAiAuthorizationObserver& observer)
      -> yac::provider::OpenAiOAuthAuth {
    observer(yac::provider::OpenAiAuthorizationNotice{
        .authorization_url =
            "https://auth.openai.com/oauth/authorize?state=manual",
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

  yac::presentation::ChatUI chat_ui;
  yac::presentation::SlashCommandRegistry registry;
  auto screen = ftxui::App::FixedSize(80, 24);
  auto component = ftxui::Renderer([] { return ftxui::text("auth"); });
  ftxui::Loop loop(&screen, component);
  yac::app::RegisterProviderAuthSlashCommandHandlers(
      registry, chat_ui, screen,
      std::make_shared<yac::cli::ProviderAuthCommand>(std::move(opts)));

  REQUIRE(registry.TryDispatch("/auth openai login"));
  REQUIRE(chat_ui.GetNotices().size() == 1);
  REQUIRE(chat_ui.GetNotices().front().notice.title ==
          "Starting OpenAI auth...");

  bool saw_fallback = false;
  for (int attempt = 0; attempt < 200 && !saw_fallback; ++attempt) {
    loop.RunOnce();
    if (chat_ui.GetNotices().size() >= 2) {
      const auto& fallback = chat_ui.GetNotices()[1].notice;
      saw_fallback =
          fallback.title == "browser launch failed; open this URL manually" &&
          fallback.detail ==
              "https://auth.openai.com/oauth/authorize?state=manual";
    }
    if (!saw_fallback) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  REQUIRE(saw_fallback);
  REQUIRE_FALSE(store->Load().has_value());

  release->store(true);
  for (int attempt = 0; attempt < 200 && !store->Load().has_value();
       ++attempt) {
    loop.RunOnce();
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }
  REQUIRE(store->Load().has_value());
}
