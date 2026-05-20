#include "provider/openai_auth_flow.hpp"

#include "openai_auth_test_helpers.hpp"

#include "mcp/oauth/pkce.hpp"

#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;

namespace yac::provider::test {
namespace {

using yac::tests::openai_auth::FakeBrowserLauncher;
using yac::tests::openai_auth::HttpRequest;
using yac::tests::openai_auth::HttpResponse;
using yac::tests::openai_auth::MakeAccountIdJwtLikeToken;
using yac::tests::openai_auth::MakeUnsignedJwtLikeToken;
using yac::tests::openai_auth::TestHttpServer;

class MemoryAuthBackend : public IOpenAiAuthBackend {
 public:
  [[nodiscard]] std::optional<std::string> Get() const override {
    std::scoped_lock lock(mutex_);
    return value_;
  }

  void Set(std::string_view auth_json) override {
    std::scoped_lock lock(mutex_);
    value_ = std::string(auth_json);
    writes_.push_back(*value_);
  }

  void Erase() override {
    std::scoped_lock lock(mutex_);
    value_.reset();
  }

  [[nodiscard]] std::size_t WriteCount() const {
    std::scoped_lock lock(mutex_);
    return writes_.size();
  }

  [[nodiscard]] std::vector<std::string> Writes() const {
    std::scoped_lock lock(mutex_);
    return writes_;
  }

 private:
  mutable std::mutex mutex_;
  std::optional<std::string> value_;
  std::vector<std::string> writes_;
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

[[nodiscard]] std::shared_ptr<MemoryAuthBackend> MakeFileBackend() {
  return std::make_shared<MemoryAuthBackend>();
}

[[nodiscard]] std::shared_ptr<OpenAiAuthStore> MakeStore(
    const std::shared_ptr<MemoryAuthBackend>& file_backend) {
  return std::make_shared<OpenAiAuthStore>(OpenAiAuthStore::Dependencies{
      .keychain_backend = std::make_shared<ThrowingKeychainBackend>(),
      .file_backend = file_backend,
  });
}

[[nodiscard]] OpenAiAuthFlow::Dependencies MakeDependencies(
    std::string issuer_url, std::shared_ptr<OpenAiAuthStore> store,
    std::function<bool(std::string_view)> browser_launcher = {}) {
  OpenAiAuthFlow::Dependencies dependencies;
  dependencies.issuer_url = std::move(issuer_url);
  dependencies.auth_store = std::move(store);
  dependencies.browser_launcher = std::move(browser_launcher);
  dependencies.code_verifier_generator = [] { return std::string("verifier-123"); };
  dependencies.code_challenge_deriver = [](std::string_view verifier) {
    return yac::mcp::oauth::DeriveCodeChallenge(verifier);
  };
  dependencies.state_generator = [] { return std::string("state-123"); };
  dependencies.clock = [] {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
  };
  return dependencies;
}

}  // namespace

TEST_CASE("browser_flow_builds_pkce_url_and_persists_tokens",
          "[openai_auth_flow]") {
  std::mutex notice_mutex;
  std::optional<OpenAiAuthorizationNotice> notice;
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = std::string(R"({"access_token":")") +
                MakeUnsignedJwtLikeToken(
                    R"({"organizations":[{"id":"org-from-access"}]})") +
                R"(","refresh_token":"refresh-1","id_token":")" +
                MakeAccountIdJwtLikeToken("acct-from-id") +
                R"(","expires_in":3600})",
    };
  });
  FakeBrowserLauncher launcher;
  const auto file_backend = MakeFileBackend();
  OpenAiAuthFlow flow(MakeDependencies(
      server.Url(""), MakeStore(file_backend),
      [&launcher](std::string_view url) { return launcher.LaunchBrowser(url); }));

  auto worker = std::async(std::launch::async, [&] {
    return flow.RunBrowserAuthorization(
        [&notice_mutex, &notice](const OpenAiAuthorizationNotice& current) {
          std::scoped_lock lock(notice_mutex);
          notice = current;
        });
  });

  for (int attempt = 0; attempt < 100; ++attempt) {
    {
      std::scoped_lock lock(notice_mutex);
      if (notice.has_value()) {
        break;
      }
    }
    std::this_thread::sleep_for(10ms);
  }

  REQUIRE(notice.has_value());
  REQUIRE(notice->browser_launched);
  REQUIRE(launcher.Urls() == std::vector<std::string>{notice->authorization_url});
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("response_type=code"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("client_id=app_EMoamEEZ73f0CkXaXp7hrann"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("scope=openid%20profile%20email%20offline_access"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("code_challenge_method=S256"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("id_token_add_organizations=true"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("codex_cli_simplified_flow=true"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("originator=yac"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("redirect_uri="));

  CURL* curl = curl_easy_init();
  REQUIRE(curl != nullptr);
  const auto cleanup = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup)> handle(curl, cleanup);
  const std::string callback_url = notice->redirect_uri +
                                   "?code=code-123&state=state-123";
  curl_easy_setopt(curl, CURLOPT_URL, callback_url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  REQUIRE(curl_easy_perform(curl) == CURLE_OK);

  const OpenAiOAuthAuth auth = worker.get();
  REQUIRE(auth.refresh_token == "refresh-1");
  REQUIRE(auth.account_id == std::optional<std::string>{"acct-from-id"});
  REQUIRE(auth.expires_at == std::chrono::system_clock::time_point{
                                 std::chrono::seconds{4600}});

  const auto requests = server.Requests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].path == "/oauth/token");
  REQUIRE_THAT(requests[0].body,
               ContainsSubstring("grant_type=authorization_code"));
  REQUIRE_THAT(requests[0].body, ContainsSubstring("code=code-123"));
  REQUIRE_THAT(requests[0].body,
               ContainsSubstring("code_verifier=verifier-123"));
  REQUIRE_THAT(requests[0].body,
               ContainsSubstring("client_id=app_EMoamEEZ73f0CkXaXp7hrann"));
  REQUIRE_THAT(requests[0].body,
               ContainsSubstring("redirect_uri="));

  const auto stored = flow.LoadStoredAuth();
  REQUIRE(stored.has_value());
  const auto* stored_oauth = std::get_if<OpenAiOAuthAuth>(&stored->auth);
  REQUIRE(stored_oauth != nullptr);
  REQUIRE(stored_oauth->refresh_token == "refresh-1");
  REQUIRE(file_backend->WriteCount() == 1);
}

TEST_CASE("browser_launch_failure_still_reports_url_and_completes",
          "[openai_auth_flow]") {
  std::optional<OpenAiAuthorizationNotice> notice;
  std::mutex notice_mutex;
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = R"({"access_token":"access-1","refresh_token":"refresh-2","expires_in":1200})",
    };
  });
  const auto file_backend = MakeFileBackend();
  OpenAiAuthFlow flow(MakeDependencies(
      server.Url(""), MakeStore(file_backend),
      [](std::string_view url) {
        (void)url;
        return false;
      }));

  auto worker = std::async(std::launch::async, [&] {
    return flow.RunBrowserAuthorization(
        [&notice_mutex, &notice](const OpenAiAuthorizationNotice& current) {
          std::scoped_lock lock(notice_mutex);
          notice = current;
        });
  });

  for (int attempt = 0; attempt < 100; ++attempt) {
    {
      std::scoped_lock lock(notice_mutex);
      if (notice.has_value()) {
        break;
      }
    }
    std::this_thread::sleep_for(10ms);
  }

  REQUIRE(notice.has_value());
  REQUIRE_FALSE(notice->browser_launched);
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("originator=yac"));

  CURL* curl = curl_easy_init();
  REQUIRE(curl != nullptr);
  const auto cleanup = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup)> handle(curl, cleanup);
  const std::string callback_url = notice->redirect_uri +
                                   "?code=code-manual&state=state-123";
  curl_easy_setopt(curl, CURLOPT_URL, callback_url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  REQUIRE(curl_easy_perform(curl) == CURLE_OK);

  const OpenAiOAuthAuth auth = worker.get();
  REQUIRE(auth.refresh_token == "refresh-2");
}

TEST_CASE("state_mismatch_is_rejected", "[openai_auth_flow]") {
  std::optional<OpenAiAuthorizationNotice> notice;
  std::mutex notice_mutex;
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = R"({"access_token":"access-1","refresh_token":"refresh-2","expires_in":1200})",
    };
  });
  const auto file_backend = MakeFileBackend();
  OpenAiAuthFlow flow(MakeDependencies(
      server.Url(""), MakeStore(file_backend),
      [](std::string_view url) {
        (void)url;
        return true;
      }));

  auto worker = std::async(std::launch::async, [&] {
    return flow.RunBrowserAuthorization(
        [&notice_mutex, &notice](const OpenAiAuthorizationNotice& current) {
          std::scoped_lock lock(notice_mutex);
          notice = current;
        });
  });

  for (int attempt = 0; attempt < 100; ++attempt) {
    {
      std::scoped_lock lock(notice_mutex);
      if (notice.has_value()) {
        break;
      }
    }
    std::this_thread::sleep_for(10ms);
  }

  REQUIRE(notice.has_value());

  CURL* curl = curl_easy_init();
  REQUIRE(curl != nullptr);
  const auto cleanup = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup)> handle(curl, cleanup);
  const std::string callback_url = notice->redirect_uri +
                                   "?code=code-bad&state=wrong-state";
  curl_easy_setopt(curl, CURLOPT_URL, callback_url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  REQUIRE(curl_easy_perform(curl) == CURLE_OK);

  REQUIRE_THROWS_WITH(worker.get(), ContainsSubstring("state mismatch"));
}

TEST_CASE("refresh_uses_skew_rotates_token_and_persists", "[openai_auth_flow]") {
  std::vector<std::chrono::system_clock::time_point> times = {
      std::chrono::system_clock::time_point{std::chrono::seconds{1885}},
      std::chrono::system_clock::time_point{std::chrono::seconds{1885}},
      std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
  };
  std::size_t index = 0;
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = std::string(R"({"access_token":")") +
                MakeUnsignedJwtLikeToken(
                    R"({"https://api.openai.com/auth.chatgpt_account_id":"acct-refreshed"})") +
                R"(","refresh_token":"refresh-rotated","expires_in":600})",
    };
  });
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  const OpenAiOAuthAuth initial{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
      .account_id = "acct-old",
  };
  (void)store->Save(initial);

  auto dependencies = MakeDependencies(server.Url(""), store);
  dependencies.clock = [&times, &index] { return times.at(index++); };
  OpenAiAuthFlow flow(std::move(dependencies));

  const OpenAiOAuthAuth refreshed = flow.RefreshIfNeeded(initial);
  REQUIRE(refreshed.access_token != "access-old");
  REQUIRE(refreshed.refresh_token == "refresh-rotated");
  REQUIRE(refreshed.account_id == std::optional<std::string>{"acct-refreshed"});
  REQUIRE(refreshed.expires_at ==
          std::chrono::system_clock::time_point{std::chrono::seconds{2600}});

  const auto requests = server.Requests();
  REQUIRE(requests.size() == 1);
  REQUIRE_THAT(requests[0].body, ContainsSubstring("grant_type=refresh_token"));
  REQUIRE_THAT(requests[0].body,
               ContainsSubstring("refresh_token=refresh-old"));

  const auto stored = flow.LoadStoredAuth();
  REQUIRE(stored.has_value());
  const auto* stored_oauth = std::get_if<OpenAiOAuthAuth>(&stored->auth);
  REQUIRE(stored_oauth != nullptr);
  REQUIRE(stored_oauth->refresh_token == "refresh-rotated");
}

TEST_CASE("missing_access_token_triggers_refresh", "[openai_auth_flow]") {
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = R"({"access_token":"access-recovered","refresh_token":"refresh-recovered","expires_in":600})",
    };
  });
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  const OpenAiOAuthAuth initial{
      .refresh_token = "refresh-old",
      .access_token = "",
      .expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{5000}},
  };
  (void)store->Save(initial);

  auto dependencies = MakeDependencies(server.Url(""), store);
  dependencies.clock = [] {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
  };
  OpenAiAuthFlow flow(std::move(dependencies));

  const OpenAiOAuthAuth refreshed = flow.RefreshIfNeeded(initial);
  REQUIRE(refreshed.access_token == "access-recovered");
  REQUIRE(refreshed.refresh_token == "refresh-recovered");
}

TEST_CASE("refresh_revocation_error_is_actionable", "[openai_auth_flow]") {
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{.status = 400,
                        .headers = {{"Content-Type", "application/json"}},
                        .body = R"({"error":"invalid_grant","error_description":"refresh token revoked"})"};
  });
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  auto dependencies = MakeDependencies(server.Url(""), store);
  dependencies.clock = [] {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
  };
  OpenAiAuthFlow flow(std::move(dependencies));

  const OpenAiOAuthAuth auth{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{1001}},
  };

  REQUIRE_THROWS_WITH(flow.RefreshIfNeeded(auth),
                      ContainsSubstring("Sign in again"));
  REQUIRE_THROWS_WITH(flow.RefreshIfNeeded(auth),
                      ContainsSubstring("refresh token revoked"));
}

TEST_CASE("concurrent_refresh_is_serialized", "[openai_auth_flow]") {
  std::atomic<int> request_count{0};
  TestHttpServer server([&request_count](const HttpRequest&, std::size_t) {
    ++request_count;
    std::this_thread::sleep_for(100ms);
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = R"({"access_token":"access-shared","refresh_token":"refresh-shared","expires_in":600})",
    };
  });
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  const OpenAiOAuthAuth initial{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at = std::chrono::system_clock::time_point{std::chrono::seconds{1001}},
  };
  (void)store->Save(initial);

  auto dependencies = MakeDependencies(server.Url(""), store);
  dependencies.clock = [] {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
  };
  OpenAiAuthFlow flow(std::move(dependencies));

  auto refresh = [&flow, &initial] { return flow.RefreshIfNeeded(initial); };
  auto first = std::async(std::launch::async, refresh);
  auto second = std::async(std::launch::async, refresh);

  const OpenAiOAuthAuth first_auth = first.get();
  const OpenAiOAuthAuth second_auth = second.get();
  REQUIRE(first_auth.access_token == "access-shared");
  REQUIRE(second_auth.access_token == "access-shared");
  REQUIRE(request_count == 1);
}

}  // namespace yac::provider::test
