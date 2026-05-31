#include "mcp/oauth/pkce.hpp"
#include "openai_auth_test_helpers.hpp"
#include "provider/openai_auth_flow.hpp"

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <chrono>
#include <curl/curl.h>
#include <future>
#include <memory>
#include <mutex>
#include <netinet/in.h>
#include <optional>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
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
using yac::tests::openai_auth::MakeFlattenedAccountIdJwtLikeToken;
using yac::tests::openai_auth::MakeUnsignedJwtLikeToken;
using yac::tests::openai_auth::TestHttpServer;

constexpr std::string_view kFixedBrowserRedirectUri =
    "http://localhost:1455/auth/callback";

class MemoryAuthBackend : public IOpenAiAuthBackend {
 public:
  explicit MemoryAuthBackend(std::optional<std::string> value = std::nullopt)
      : value_(std::move(value)) {}

  [[nodiscard]] std::optional<std::string> Get() const override {
    std::scoped_lock lock(mutex_);
    ++get_count_;
    return value_;
  }

  void Set(std::string_view auth_json) override {
    std::scoped_lock lock(mutex_);
    ++set_count_;
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

  [[nodiscard]] int GetCount() const {
    std::scoped_lock lock(mutex_);
    return get_count_;
  }

  [[nodiscard]] int SetCount() const {
    std::scoped_lock lock(mutex_);
    return set_count_;
  }

 private:
  mutable std::mutex mutex_;
  std::optional<std::string> value_;
  std::vector<std::string> writes_;
  mutable int get_count_ = 0;
  int set_count_ = 0;
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

[[nodiscard]] std::shared_ptr<MemoryAuthBackend> MakeFileBackend(
    OpenAiAuth auth) {
  return std::make_shared<MemoryAuthBackend>(SerializeOpenAiAuth(auth));
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
  dependencies.code_verifier_generator = [] {
    return std::string("verifier-123");
  };
  dependencies.code_challenge_deriver = [](std::string_view verifier) {
    return yac::mcp::oauth::DeriveCodeChallenge(verifier);
  };
  dependencies.state_generator = [] { return std::string("state-123"); };
  dependencies.clock = [] {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
  };
  return dependencies;
}

[[nodiscard]] std::string UrlDecode(std::string_view value) {
  CURL* curl = curl_easy_init();
  REQUIRE(curl != nullptr);
  const auto cleanup = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup)> handle(curl, cleanup);

  int output_length = 0;
  char* decoded = curl_easy_unescape(
      curl, value.data(), static_cast<int>(value.size()), &output_length);
  REQUIRE(decoded != nullptr);
  const auto cleanup_decoded = [](char* data) { curl_free(data); };
  std::unique_ptr<char, decltype(cleanup_decoded)> decoded_handle(
      decoded, cleanup_decoded);
  return std::string(decoded, static_cast<std::size_t>(output_length));
}

[[nodiscard]] std::optional<std::string> DecodedQueryParam(
    std::string_view url, std::string_view key) {
  const std::size_t query_start = url.find('?');
  if (query_start == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t pos = query_start + 1;
  while (pos <= url.size()) {
    const std::size_t amp = url.find('&', pos);
    const std::string_view segment =
        url.substr(pos, amp == std::string_view::npos ? std::string_view::npos
                                                      : amp - pos);
    const std::size_t equals = segment.find('=');
    if (equals != std::string_view::npos && segment.substr(0, equals) == key) {
      return UrlDecode(segment.substr(equals + 1));
    }
    if (amp == std::string_view::npos) {
      break;
    }
    pos = amp + 1;
  }
  return std::nullopt;
}

class Port1455Blocker {
 public:
  Port1455Blocker() : fd_(socket(AF_INET, SOCK_STREAM, 0)) {
    REQUIRE(fd_ >= 0);
    int enable = 1;
    REQUIRE(setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &enable,
                       sizeof(enable)) == 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(1455);
    REQUIRE(bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0);
    REQUIRE(listen(fd_, 1) == 0);
  }

  ~Port1455Blocker() {
    if (fd_ >= 0) {
      close(fd_);
    }
  }

 private:
  int fd_ = -1;
};

}  // namespace

TEST_CASE("account_id_extraction_matches_opencode_claim_precedence",
          "[openai_auth_flow]") {
  struct Case {
    std::string payload;
    std::string expected_account_id;
  };
  const std::array<Case, 4> cases = {
      Case{
          .payload =
              R"({"chatgpt_account_id":"acct-top","https://api.openai.com/auth":{"chatgpt_account_id":"acct-nested"},"https://api.openai.com/auth.chatgpt_account_id":"acct-legacy","organizations":[{"id":"org-fallback"}]})",
          .expected_account_id = "acct-top"},
      Case{
          .payload =
              R"({"https://api.openai.com/auth":{"chatgpt_account_id":"acct-nested"},"https://api.openai.com/auth.chatgpt_account_id":"acct-legacy","organizations":[{"id":"org-fallback"}]})",
          .expected_account_id = "acct-nested"},
      Case{
          .payload =
              R"({"https://api.openai.com/auth.chatgpt_account_id":"acct-legacy","organizations":[{"id":"org-fallback"}]})",
          .expected_account_id = "acct-legacy"},
      Case{.payload = R"({"organizations":[{"id":"org-fallback"}]})",
           .expected_account_id = "org-fallback"},
  };

  for (const auto& item : cases) {
    TestHttpServer server([&item](const HttpRequest&, std::size_t) {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body = std::string(R"({"access_token":")") +
                  MakeUnsignedJwtLikeToken(item.payload) +
                  R"(","refresh_token":"refresh-new","expires_in":600})",
      };
    });
    auto dependencies =
        MakeDependencies(server.Url(""), MakeStore(MakeFileBackend()));
    dependencies.clock = [] {
      return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
    };
    OpenAiAuthFlow flow(std::move(dependencies));
    const OpenAiOAuthAuth auth{
        .refresh_token = "refresh-old",
        .access_token = "access-old",
        .expires_at =
            std::chrono::system_clock::time_point{std::chrono::seconds{1001}},
    };

    const OpenAiOAuthAuth refreshed = flow.RefreshIfNeeded(auth);
    REQUIRE(refreshed.account_id ==
            std::optional<std::string>{item.expected_account_id});
  }
}

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
  OpenAiAuthFlow flow(MakeDependencies(server.Url(""), MakeStore(file_backend),
                                       [&launcher](std::string_view url) {
                                         return launcher.LaunchBrowser(url);
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
  REQUIRE(notice->browser_launched);
  REQUIRE(launcher.Urls() ==
          std::vector<std::string>{notice->authorization_url});
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("response_type=code"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("client_id=app_EMoamEEZ73f0CkXaXp7hrann"));
  REQUIRE_THAT(
      notice->authorization_url,
      ContainsSubstring("scope=openid%20profile%20email%20offline_access"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("code_challenge_method=S256"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("id_token_add_organizations=true"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("codex_cli_simplified_flow=true"));
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("originator=opencode"));
  REQUIRE_THAT(notice->authorization_url, ContainsSubstring("redirect_uri="));
  REQUIRE(notice->redirect_uri == kFixedBrowserRedirectUri);
  REQUIRE(DecodedQueryParam(notice->authorization_url, "redirect_uri") ==
          std::optional<std::string>{std::string(kFixedBrowserRedirectUri)});
  REQUIRE(DecodedQueryParam(notice->authorization_url, "originator") ==
          std::optional<std::string>{"opencode"});
  REQUIRE(DecodedQueryParam(notice->authorization_url, "client_id") ==
          std::optional<std::string>{"app_EMoamEEZ73f0CkXaXp7hrann"});
  REQUIRE(DecodedQueryParam(notice->authorization_url, "response_type") ==
          std::optional<std::string>{"code"});

  CURL* curl = curl_easy_init();
  REQUIRE(curl != nullptr);
  const auto cleanup = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup)> handle(curl, cleanup);
  const std::string callback_url =
      notice->redirect_uri + "?code=code-123&state=state-123";
  curl_easy_setopt(curl, CURLOPT_URL, callback_url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  REQUIRE(curl_easy_perform(curl) == CURLE_OK);

  const OpenAiOAuthAuth auth = worker.get();
  REQUIRE(auth.refresh_token == "refresh-1");
  REQUIRE(auth.account_id == std::optional<std::string>{"acct-from-id"});
  REQUIRE(auth.expires_at ==
          std::chrono::system_clock::time_point{std::chrono::seconds{4600}});

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
  REQUIRE_THAT(
      requests[0].body,
      ContainsSubstring("redirect_uri=http%3A%2F%2Flocalhost%3A1455%2Fauth%2F"
                        "callback"));

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
        .body =
            R"({"access_token":"access-1","refresh_token":"refresh-2","expires_in":1200})",
    };
  });
  const auto file_backend = MakeFileBackend();
  OpenAiAuthFlow flow(MakeDependencies(server.Url(""), MakeStore(file_backend),
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
  REQUIRE(notice->redirect_uri == kFixedBrowserRedirectUri);
  REQUIRE_THAT(notice->authorization_url,
               ContainsSubstring("originator=opencode"));

  CURL* curl = curl_easy_init();
  REQUIRE(curl != nullptr);
  const auto cleanup = [](CURL* handle) { curl_easy_cleanup(handle); };
  std::unique_ptr<CURL, decltype(cleanup)> handle(curl, cleanup);
  const std::string callback_url =
      notice->redirect_uri + "?code=code-manual&state=state-123";
  curl_easy_setopt(curl, CURLOPT_URL, callback_url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  REQUIRE(curl_easy_perform(curl) == CURLE_OK);

  const OpenAiOAuthAuth auth = worker.get();
  REQUIRE(auth.refresh_token == "refresh-2");
}

TEST_CASE("browser_flow_token_errors_do_not_leak_auth_code_or_verifier",
          "[openai_auth_flow]") {
  std::optional<OpenAiAuthorizationNotice> notice;
  std::mutex notice_mutex;
  const std::string raw_jwt_like_secret = "header.payload.signature";
  TestHttpServer server([&raw_jwt_like_secret](const HttpRequest&,
                                               std::size_t) {
    return HttpResponse{
        .status = 400,
        .headers = {{"Content-Type", "application/json"}},
        .body =
            std::string(R"({"error":"invalid_grant","error_description":")") +
            "code-browser-secret verifier-123 " + raw_jwt_like_secret + R"("})",
    };
  });
  const auto file_backend = MakeFileBackend();
  OpenAiAuthFlow flow(MakeDependencies(server.Url(""), MakeStore(file_backend),
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
  const std::string callback_url =
      notice->redirect_uri + "?code=code-browser-secret&state=state-123";
  curl_easy_setopt(curl, CURLOPT_URL, callback_url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  REQUIRE(curl_easy_perform(curl) == CURLE_OK);

  try {
    static_cast<void>(worker.get());
    FAIL("browser auth should fail");
  } catch (const std::exception& error) {
    const std::string message = error.what();
    REQUIRE(message.find("OpenAI OAuth token request failed") !=
            std::string::npos);
    REQUIRE(message.find("code-browser-secret") == std::string::npos);
    REQUIRE(message.find("verifier-123") == std::string::npos);
    REQUIRE(message.find(raw_jwt_like_secret) == std::string::npos);
  }
}

TEST_CASE("state_mismatch_is_rejected", "[openai_auth_flow]") {
  std::optional<OpenAiAuthorizationNotice> notice;
  std::mutex notice_mutex;
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"access_token":"access-1","refresh_token":"refresh-2","expires_in":1200})",
    };
  });
  const auto file_backend = MakeFileBackend();
  OpenAiAuthFlow flow(MakeDependencies(server.Url(""), MakeStore(file_backend),
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
  const std::string callback_url =
      notice->redirect_uri + "?code=code-bad&state=wrong-state";
  curl_easy_setopt(curl, CURLOPT_URL, callback_url.c_str());
  curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, 3000L);
  REQUIRE(curl_easy_perform(curl) == CURLE_OK);

  REQUIRE_THROWS_WITH(worker.get(), ContainsSubstring("state mismatch"));
}

TEST_CASE("browser_flow_fails_fast_when_fixed_callback_port_is_unavailable",
          "[openai_auth_flow]") {
  Port1455Blocker blocker;
  std::atomic<bool> browser_called{false};
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = R"({"access_token":"access-1"})",
    };
  });
  const auto file_backend = MakeFileBackend();
  OpenAiAuthFlow flow(MakeDependencies(server.Url(""), MakeStore(file_backend),
                                       [&browser_called](std::string_view url) {
                                         (void)url;
                                         browser_called = true;
                                         return true;
                                       }));

  const auto start = std::chrono::steady_clock::now();
  REQUIRE_THROWS_WITH(
      flow.RunBrowserAuthorization(),
      ContainsSubstring("OpenAI OAuth callback port 1455 is unavailable; free "
                        "the port and retry"));
  REQUIRE(std::chrono::steady_clock::now() - start < 500ms);
  REQUIRE_FALSE(browser_called);
  REQUIRE(server.Requests().empty());
}

TEST_CASE("device_flow_starts_polls_exchanges_and_persists_tokens",
          "[openai_auth_flow]") {
  std::vector<std::chrono::milliseconds> sleeps;
  TestHttpServer server([](const HttpRequest& request, std::size_t index) {
    if (request.path == "/api/accounts/deviceauth/usercode") {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body =
              R"({"device_auth_id":"device-123","user_code":"ABCD-EFGH","interval":2})",
      };
    }
    if (request.path == "/api/accounts/deviceauth/token" && index == 1) {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body = R"({"status":"pending","interval":1})",
      };
    }
    if (request.path == "/api/accounts/deviceauth/token") {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body =
              R"({"authorization_code":"auth-code-secret","code_verifier":"verifier-secret"})",
      };
    }
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = std::string(R"({"access_token":")") +
                MakeAccountIdJwtLikeToken("acct-device") +
                R"(","refresh_token":"refresh-device","expires_in":3600})",
    };
  });
  const auto file_backend = MakeFileBackend();
  auto dependencies = MakeDependencies(server.Url(""), MakeStore(file_backend));
  dependencies.sleep_for = [&sleeps](std::chrono::milliseconds duration,
                                     std::stop_token stop_token) {
    REQUIRE_FALSE(stop_token.stop_requested());
    sleeps.push_back(duration);
    return true;
  };
  OpenAiAuthFlow flow(std::move(dependencies));
  std::optional<OpenAiDeviceAuthorizationNotice> notice;

  const OpenAiOAuthAuth auth = flow.RunDeviceAuthorization(
      [&notice](const OpenAiDeviceAuthorizationNotice& current) {
        notice = current;
      });

  REQUIRE(notice.has_value());
  REQUIRE(notice->verification_url == "https://auth.openai.com/codex/device");
  REQUIRE(notice->user_code == "ABCD-EFGH");
  REQUIRE(sleeps == std::vector<std::chrono::milliseconds>{5000ms, 4000ms});
  REQUIRE(auth.refresh_token == "refresh-device");
  REQUIRE(auth.account_id == std::optional<std::string>{"acct-device"});

  const auto requests = server.Requests();
  REQUIRE(requests.size() == 4);
  REQUIRE(requests[0].method == "POST");
  REQUIRE(requests[0].path == "/api/accounts/deviceauth/usercode");
  REQUIRE(requests[0].headers.at("User-Agent") == "opencode/0.1.0");
  REQUIRE(requests[0].body ==
          R"({"client_id":"app_EMoamEEZ73f0CkXaXp7hrann"})");
  REQUIRE(requests[1].path == "/api/accounts/deviceauth/token");
  REQUIRE_THAT(requests[1].body, ContainsSubstring("device-123"));
  REQUIRE_THAT(requests[1].body, ContainsSubstring("ABCD-EFGH"));
  REQUIRE(requests[3].path == "/oauth/token");
  REQUIRE_THAT(requests[3].body,
               ContainsSubstring("grant_type=authorization_code"));
  REQUIRE_THAT(requests[3].body, ContainsSubstring("code=auth-code-secret"));
  REQUIRE_THAT(requests[3].body,
               ContainsSubstring("code_verifier=verifier-secret"));
  REQUIRE_THAT(requests[3].body,
               ContainsSubstring("redirect_uri=https%3A%2F%2Fauth.openai.com%2F"
                                 "deviceauth%2Fcallback"));

  const auto stored = flow.LoadStoredAuth();
  REQUIRE(stored.has_value());
  const auto* stored_oauth = std::get_if<OpenAiOAuthAuth>(&stored->auth);
  REQUIRE(stored_oauth != nullptr);
  REQUIRE(stored_oauth->refresh_token == "refresh-device");
}

TEST_CASE("device_flow_denial_and_failed_states_are_actionable",
          "[openai_auth_flow]") {
  for (const auto* status : {"denied", "failed"}) {
    TestHttpServer server([status](const HttpRequest& request, std::size_t) {
      if (request.path == "/api/accounts/deviceauth/usercode") {
        return HttpResponse{
            .headers = {{"Content-Type", "application/json"}},
            .body =
                R"({"device_auth_id":"device-123","user_code":"ABCD-EFGH","interval":0})",
        };
      }
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body = std::string(R"({"status":")") + status + R"("})"};
    });
    auto dependencies =
        MakeDependencies(server.Url(""), MakeStore(MakeFileBackend()));
    dependencies.sleep_for = [](std::chrono::milliseconds, std::stop_token) {
      return true;
    };
    OpenAiAuthFlow flow(std::move(dependencies));

    try {
      static_cast<void>(flow.RunDeviceAuthorization());
      FAIL("device auth should fail");
    } catch (const std::exception& error) {
      const std::string message = error.what();
      REQUIRE(message.find("OpenAI device auth") != std::string::npos);
      REQUIRE(message.find("yac auth openai login --device") !=
              std::string::npos);
      REQUIRE(message.find("auth-code") == std::string::npos);
      REQUIRE(message.find("verifier") == std::string::npos);
    }
  }
}

TEST_CASE("device_flow_stop_token_stops_polling", "[openai_auth_flow]") {
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    if (request.path == "/api/accounts/deviceauth/usercode") {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body =
              R"({"device_auth_id":"device-123","user_code":"ABCD-EFGH","interval":1})",
      };
    }
    return HttpResponse{.headers = {{"Content-Type", "application/json"}},
                        .body = R"({"status":"pending"})"};
  });
  auto dependencies =
      MakeDependencies(server.Url(""), MakeStore(MakeFileBackend()));
  dependencies.sleep_for = [](std::chrono::milliseconds, std::stop_token) {
    return false;
  };
  OpenAiAuthFlow flow(std::move(dependencies));

  REQUIRE_THROWS_WITH(flow.RunDeviceAuthorization(),
                      ContainsSubstring("stopped before approval"));
  REQUIRE(server.Requests().size() == 1);
}

TEST_CASE("device_flow_token_errors_do_not_leak_auth_code_or_verifier",
          "[openai_auth_flow]") {
  TestHttpServer server([](const HttpRequest& request, std::size_t) {
    if (request.path == "/api/accounts/deviceauth/usercode") {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body =
              R"({"device_auth_id":"device-123","user_code":"ABCD-EFGH","interval":0})",
      };
    }
    if (request.path == "/api/accounts/deviceauth/token") {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body =
              R"({"authorization_code":"auth-code-secret","code_verifier":"verifier-secret"})",
      };
    }
    return HttpResponse{
        .status = 400,
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"error":"invalid_grant","error_description":"auth-code-secret verifier-secret access-token-secret"})",
    };
  });
  auto dependencies =
      MakeDependencies(server.Url(""), MakeStore(MakeFileBackend()));
  dependencies.sleep_for = [](std::chrono::milliseconds, std::stop_token) {
    return true;
  };
  OpenAiAuthFlow flow(std::move(dependencies));

  try {
    static_cast<void>(flow.RunDeviceAuthorization());
    FAIL("device auth should fail");
  } catch (const std::exception& error) {
    const std::string message = error.what();
    REQUIRE(message.find("OpenAI OAuth token request failed") !=
            std::string::npos);
    REQUIRE(message.find("auth-code-secret") == std::string::npos);
    REQUIRE(message.find("verifier-secret") == std::string::npos);
    REQUIRE(message.find("access-token-secret") == std::string::npos);
  }
}

TEST_CASE("refresh_ignores_malformed_jwt_and_missing_account_claim",
          "[openai_auth_flow]") {
  const std::string malformed_id_token = "header.not-base64.signature";
  TestHttpServer server([&malformed_id_token](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = std::string(R"({"access_token":")") +
                MakeUnsignedJwtLikeToken(R"({"email":"user@example.test"})") +
                R"(","refresh_token":"refresh-no-claim","id_token":")" +
                malformed_id_token + R"(","expires_in":600})",
    };
  });
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  auto dependencies = MakeDependencies(server.Url(""), store);
  dependencies.clock = [] {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
  };
  OpenAiAuthFlow flow(std::move(dependencies));
  const OpenAiOAuthAuth initial{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{1001}},
  };

  const OpenAiOAuthAuth refreshed = flow.RefreshIfNeeded(initial);

  REQUIRE(refreshed.refresh_token == "refresh-no-claim");
  REQUIRE_FALSE(refreshed.account_id.has_value());
}

TEST_CASE("device_flow_missing_access_token_does_not_leak_exchange_secrets",
          "[openai_auth_flow]") {
  const std::string raw_jwt_like_secret = "header.payload.signature";
  TestHttpServer server([&raw_jwt_like_secret](const HttpRequest& request,
                                               std::size_t) {
    if (request.path == "/api/accounts/deviceauth/usercode") {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body =
              R"({"device_auth_id":"device-123","user_code":"ABCD-EFGH","interval":0})",
      };
    }
    if (request.path == "/api/accounts/deviceauth/token") {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body =
              R"({"authorization_code":"auth-code-secret","code_verifier":"verifier-secret"})",
      };
    }
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = std::string(R"({"id_token":")") + raw_jwt_like_secret +
                R"(","refresh_token":"refresh-secret","expires_in":600})",
    };
  });
  auto dependencies =
      MakeDependencies(server.Url(""), MakeStore(MakeFileBackend()));
  dependencies.sleep_for = [](std::chrono::milliseconds, std::stop_token) {
    return true;
  };
  OpenAiAuthFlow flow(std::move(dependencies));

  try {
    static_cast<void>(flow.RunDeviceAuthorization());
    FAIL("device auth should fail");
  } catch (const std::exception& error) {
    const std::string message = error.what();
    REQUIRE(message.find("missing access_token") != std::string::npos);
    REQUIRE(message.find("auth-code-secret") == std::string::npos);
    REQUIRE(message.find("verifier-secret") == std::string::npos);
    REQUIRE(message.find(raw_jwt_like_secret) == std::string::npos);
  }
}

TEST_CASE("refresh_uses_skew_rotates_token_and_persists",
          "[openai_auth_flow]") {
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
                MakeFlattenedAccountIdJwtLikeToken("acct-refreshed") +
                R"(","refresh_token":"refresh-rotated","expires_in":600})",
    };
  });
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  const OpenAiOAuthAuth initial{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{2000}},
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

TEST_CASE("refresh_updates_cache_without_reread", "[openai_auth_flow]") {
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = std::string(R"({"access_token":")") +
                MakeFlattenedAccountIdJwtLikeToken("acct-refreshed") +
                R"(","refresh_token":"refresh-rotated","expires_in":600})",
    };
  });
  const OpenAiOAuthAuth initial{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{1001}},
      .account_id = "acct-old",
  };
  const auto file_backend = MakeFileBackend(initial);
  const auto store = MakeStore(file_backend);

  auto dependencies = MakeDependencies(server.Url(""), store);
  dependencies.clock = [] {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
  };
  OpenAiAuthFlow flow(std::move(dependencies));

  const OpenAiOAuthAuth refreshed = flow.RefreshIfNeeded(initial);
  REQUIRE(refreshed.access_token != "access-old");
  REQUIRE(refreshed.refresh_token == "refresh-rotated");
  REQUIRE(refreshed.account_id == std::optional<std::string>{"acct-refreshed"});
  REQUIRE(file_backend->GetCount() == 1);
  REQUIRE(file_backend->SetCount() == 1);
  REQUIRE(file_backend->WriteCount() == 1);

  const auto stored = flow.LoadStoredAuth();
  REQUIRE(stored.has_value());
  const auto* stored_oauth = std::get_if<OpenAiOAuthAuth>(&stored->auth);
  REQUIRE(stored_oauth != nullptr);
  REQUIRE(stored_oauth->access_token == refreshed.access_token);
  REQUIRE(stored_oauth->refresh_token == "refresh-rotated");
  REQUIRE(stored_oauth->account_id ==
          std::optional<std::string>{"acct-refreshed"});
  REQUIRE(file_backend->GetCount() == 1);
  REQUIRE(file_backend->SetCount() == 1);
}

TEST_CASE("fresh_oauth_uses_cache_without_refresh_save", "[openai_auth_flow]") {
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"access_token":"unexpected","refresh_token":"unexpected","expires_in":600})",
    };
  });
  const OpenAiOAuthAuth initial{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{5000}},
      .account_id = "acct-old",
  };
  const auto file_backend = MakeFileBackend(initial);
  const auto store = MakeStore(file_backend);
  REQUIRE(store->Load().has_value());
  REQUIRE(file_backend->GetCount() == 1);

  auto dependencies = MakeDependencies(server.Url(""), store);
  dependencies.clock = [] {
    return std::chrono::system_clock::time_point{std::chrono::seconds{1000}};
  };
  OpenAiAuthFlow flow(std::move(dependencies));

  const OpenAiOAuthAuth unchanged = flow.RefreshIfNeeded(initial);
  REQUIRE(unchanged.access_token == "access-old");
  REQUIRE(unchanged.refresh_token == "refresh-old");
  const auto stored = flow.LoadStoredAuth();
  REQUIRE(stored.has_value());
  const auto* stored_oauth = std::get_if<OpenAiOAuthAuth>(&stored->auth);
  REQUIRE(stored_oauth != nullptr);
  REQUIRE(stored_oauth->access_token == "access-old");
  REQUIRE(stored_oauth->refresh_token == "refresh-old");
  REQUIRE(file_backend->GetCount() == 1);
  REQUIRE(file_backend->SetCount() == 0);
  REQUIRE(server.Requests().empty());
}

TEST_CASE("missing_access_token_triggers_refresh", "[openai_auth_flow]") {
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"access_token":"access-recovered","refresh_token":"refresh-recovered","expires_in":600})",
    };
  });
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  const OpenAiOAuthAuth initial{
      .refresh_token = "refresh-old",
      .access_token = "",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{5000}},
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
    return HttpResponse{
        .status = 400,
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"error":"invalid_grant","error_description":"refresh token revoked"})"};
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
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{1001}},
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
        .body =
            R"({"access_token":"access-shared","refresh_token":"refresh-shared","expires_in":600})",
    };
  });
  const auto file_backend = MakeFileBackend();
  const auto store = MakeStore(file_backend);
  const OpenAiOAuthAuth initial{
      .refresh_token = "refresh-old",
      .access_token = "access-old",
      .expires_at =
          std::chrono::system_clock::time_point{std::chrono::seconds{1001}},
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
