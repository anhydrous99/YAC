#include "openai_auth_test_helpers.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::ContainsSubstring;
using namespace yac::tests::openai_auth;

namespace {

[[nodiscard]] std::string DecodeBase64Url(std::string_view input) {
  auto decode_char = [](char c) -> int {
    if (c >= 'A' && c <= 'Z') {
      return c - 'A';
    }
    if (c >= 'a' && c <= 'z') {
      return c - 'a' + 26;
    }
    if (c >= '0' && c <= '9') {
      return c - '0' + 52;
    }
    if (c == '-') {
      return 62;
    }
    if (c == '_') {
      return 63;
    }
    return -1;
  };

  std::string output;
  int buffer = 0;
  int bits = 0;
  for (char c : input) {
    const int value = decode_char(c);
    if (value < 0) {
      throw std::runtime_error("invalid base64url input");
    }
    buffer = (buffer << 6) | value;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      output.push_back(static_cast<char>((buffer >> bits) & 0xff));
    }
  }
  return output;
}

[[nodiscard]] std::string DecodeJwtPayload(std::string_view token) {
  const auto first_dot = token.find('.');
  const auto second_dot = token.find('.', first_dot + 1);

  REQUIRE(first_dot != std::string::npos);
  REQUIRE(second_dot != std::string::npos);

  return DecodeBase64Url(
      token.substr(first_dot + 1, second_dot - first_dot - 1));
}

[[nodiscard]] unsigned short PortFromUrl(std::string_view url) {
  const auto host_start = url.find("127.0.0.1");
  REQUIRE(host_start != std::string_view::npos);
  const auto port_start = url.find(':', host_start) + 1;
  const auto port_end = url.find('/', port_start);
  const std::string port =
      std::string(url.substr(port_start, port_end - port_start));
  return static_cast<unsigned short>(std::stoul(port));
}

[[nodiscard]] int ConnectToServer(const TestHttpServer& server,
                                  std::string_view path) {
  const unsigned short port = PortFromUrl(server.Url(path));
  const int sock = socket(AF_INET, SOCK_STREAM, 0);
  REQUIRE(sock >= 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  REQUIRE(connect(sock, reinterpret_cast<const sockaddr*>(&addr),
                  sizeof(addr)) == 0);
  return sock;
}

void RequireDestroyCompletesWithOpenClient(
    std::unique_ptr<TestHttpServer> server, int client_fd) {
  std::this_thread::sleep_for(std::chrono::milliseconds(50));

  auto destroy = std::async(std::launch::async,
                            [server = std::move(server)]() mutable {
                              server.reset();
                            });
  const bool completed = destroy.wait_for(std::chrono::seconds(1)) ==
                         std::future_status::ready;
  close(client_fd);

  if (!completed) {
    REQUIRE(destroy.wait_for(std::chrono::seconds(1)) ==
            std::future_status::ready);
  }
  REQUIRE(completed);
}

}  // namespace

TEST_CASE("http_server_captures_requests") {
  TestHttpServer server([](const HttpRequest& request, std::size_t index) {
    REQUIRE(index == 0);
    REQUIRE(request.method == "POST");
    REQUIRE(request.path == "/token");
    REQUIRE_THAT(request.body,
                 ContainsSubstring("grant_type=authorization_code"));
    return HttpResponse{.status = 200,
                        .headers = {{"Content-Type", "text/plain"}},
                        .body = "ok"};
  });

  const std::string body = "grant_type=authorization_code&code=test";
  const std::string request =
      "POST /token HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Content-Length: " +
      std::to_string(body.size()) +
      "\r\n"
      "\r\n" +
      body;

  const auto url = server.Url("/token");
  REQUIRE_THAT(url, ContainsSubstring("http://127.0.0.1:"));

  const int sock = ConnectToServer(server, "/token");
  REQUIRE(send(sock, request.data(), request.size(), 0) ==
          static_cast<ssize_t>(request.size()));

  std::array<char, 64> buffer{};
  REQUIRE(recv(sock, buffer.data(), buffer.size(), 0) > 0);
  close(sock);

  const auto requests = server.Requests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].path == "/token");
}

TEST_CASE("http_server_repeated_idle_teardown_is_stable") {
  std::atomic<int> request_count = 0;

  for (int iteration = 0; iteration < 100; ++iteration) {
    TestHttpServer server([&request_count](const HttpRequest&, std::size_t) {
      request_count.fetch_add(1);
      return HttpResponse{.status = 204};
    });
    REQUIRE_THAT(server.Url("/idle"), ContainsSubstring("http://127.0.0.1:"));
  }

  REQUIRE(request_count.load() == 0);
}

TEST_CASE("http_server_request_then_immediate_teardown_is_stable") {
  std::atomic<int> request_count = 0;

  for (int iteration = 0; iteration < 50; ++iteration) {
    {
      TestHttpServer server([&request_count](const HttpRequest& request,
                                             std::size_t index) {
        REQUIRE(request.path == "/ping");
        REQUIRE(index == 0);
        request_count.fetch_add(1);
        return HttpResponse{.status = 200, .body = "ok"};
      });

      const std::string request =
          "GET /ping HTTP/1.1\r\n"
          "Host: 127.0.0.1\r\n"
          "Content-Length: 0\r\n"
          "\r\n";
      const int sock = ConnectToServer(server, "/ping");
      REQUIRE(send(sock, request.data(), request.size(), 0) ==
              static_cast<ssize_t>(request.size()));

      std::array<char, 64> buffer{};
      REQUIRE(recv(sock, buffer.data(), buffer.size(), 0) > 0);
      close(sock);
    }

    REQUIRE(request_count.load() == iteration + 1);
  }
}

TEST_CASE("http_server_teardown_stops_client_that_sends_no_bytes") {
  std::atomic<int> request_count = 0;
  auto server = std::make_unique<TestHttpServer>(
      [&request_count](const HttpRequest&, std::size_t) {
        request_count.fetch_add(1);
        return HttpResponse{.status = 200, .body = "unexpected"};
      });

  const int sock = ConnectToServer(*server, "/never-sent");
  RequireDestroyCompletesWithOpenClient(std::move(server), sock);

  REQUIRE(request_count.load() == 0);
}

TEST_CASE("http_server_teardown_stops_client_with_partial_body") {
  std::atomic<int> request_count = 0;
  auto server = std::make_unique<TestHttpServer>(
      [&request_count](const HttpRequest&, std::size_t) {
        request_count.fetch_add(1);
        return HttpResponse{.status = 200, .body = "unexpected"};
      });

  const std::string request =
      "POST /partial HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Content-Length: 32\r\n"
      "\r\n"
      "short";
  const int sock = ConnectToServer(*server, "/partial");
  REQUIRE(send(sock, request.data(), request.size(), 0) ==
          static_cast<ssize_t>(request.size()));
  RequireDestroyCompletesWithOpenClient(std::move(server), sock);

  REQUIRE(request_count.load() == 0);
}

TEST_CASE("http_server_teardown_stops_client_not_reading_large_response") {
  std::atomic<int> request_count = 0;
  auto server = std::make_unique<TestHttpServer>(
      [&request_count](const HttpRequest& request, std::size_t index) {
        REQUIRE(request.path == "/large");
        REQUIRE(index == 0);
        std::string body(8 * 1024 * 1024, 'x');
        request_count.fetch_add(1);
        return HttpResponse{.status = 200, .body = body};
      });

  const int sock = ConnectToServer(*server, "/large");
  int receive_buffer_size = 4096;
  REQUIRE(setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &receive_buffer_size,
                     sizeof(receive_buffer_size)) == 0);

  const std::string request =
      "GET /large HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Content-Length: 0\r\n"
      "\r\n";
  REQUIRE(send(sock, request.data(), request.size(), 0) ==
          static_cast<ssize_t>(request.size()));

  for (int attempt = 0; attempt < 100 && request_count.load() == 0;
       ++attempt) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  REQUIRE(request_count.load() == 1);

  RequireDestroyCompletesWithOpenClient(std::move(server), sock);
}

TEST_CASE("fake_browser_launcher_records_urls") {
  FakeBrowserLauncher launcher;

  REQUIRE(launcher.LaunchBrowser("http://127.0.0.1:9999/callback?code=abc"));
  REQUIRE(launcher.LaunchBrowser("http://127.0.0.1:9999/cancel"));

  const auto urls = launcher.Urls();
  REQUIRE(urls.size() == 2);
  REQUIRE(urls[0] == "http://127.0.0.1:9999/callback?code=abc");
  REQUIRE(urls[1] == "http://127.0.0.1:9999/cancel");
}

TEST_CASE("fake_token_stores_round_trip") {
  FakeKeychainTokenStore keychain_store;
  FakeFileTokenStore file_store;

  keychain_store.Set("server-a", R"({"access_token":"a"})");
  file_store.Set("server-b", R"({"access_token":"b"})");

  REQUIRE(keychain_store.Get("server-a") ==
          std::optional<std::string>{R"({"access_token":"a"})"});
  REQUIRE(file_store.Get("server-b") ==
          std::optional<std::string>{R"({"access_token":"b"})"});

  keychain_store.Erase("server-a");
  file_store.Erase("server-b");

  REQUIRE_FALSE(keychain_store.Get("server-a").has_value());
  REQUIRE_FALSE(file_store.Get("server-b").has_value());
}

TEST_CASE("jwt_helpers_include_chatgpt_account_id") {
  const std::string token = MakeAccountIdJwtLikeToken("acct_123");
  const std::string payload = DecodeJwtPayload(token);
  REQUIRE(payload == R"({"chatgpt_account_id":"acct_123"})");

  const std::string custom = MakeUnsignedJwtLikeToken(
      R"({"chatgpt_account_id":"acct_456","organizations":[{"id":"org_1"}]})");
  REQUIRE(
      DecodeJwtPayload(custom) ==
      R"({"chatgpt_account_id":"acct_456","organizations":[{"id":"org_1"}]})");
}

TEST_CASE("jwt_helpers_cover_opencode_claim_variants") {
  REQUIRE(
      DecodeJwtPayload(MakeNestedAccountIdJwtLikeToken("acct-nested")) ==
      R"({"https://api.openai.com/auth":{"chatgpt_account_id":"acct-nested"}})");
  REQUIRE(DecodeJwtPayload(MakeAccountIdJwtLikeToken("acct-top-level")) ==
          R"({"chatgpt_account_id":"acct-top-level"})");
  REQUIRE(
      DecodeJwtPayload(MakeFlattenedAccountIdJwtLikeToken("acct-legacy")) ==
      R"({"https://api.openai.com/auth.chatgpt_account_id":"acct-legacy"})");
  REQUIRE(
      DecodeJwtPayload(MakeOrganizationFallbackJwtLikeToken("org-fallback")) ==
      R"({"organizations":[{"id":"org-fallback"}]})");
}

TEST_CASE("model_id_helpers_cover_static_dynamic_and_rejected_vectors") {
  const auto static_models = StaticAcceptedModelIds();
  REQUIRE(std::find(static_models.begin(), static_models.end(), "gpt-5.4") !=
          static_models.end());

  const auto vectors = DynamicModelIdTestVectors();
  REQUIRE(std::find_if(vectors.begin(), vectors.end(), [](const auto& item) {
            return item.id == "gpt-5.5" && item.accepted;
          }) != vectors.end());
  REQUIRE(std::find_if(vectors.begin(), vectors.end(), [](const auto& item) {
            return item.id == "gpt-5.41" && item.accepted;
          }) != vectors.end());
  REQUIRE(std::find_if(vectors.begin(), vectors.end(), [](const auto& item) {
            return item.id == "gpt-6.0" && item.accepted;
          }) != vectors.end());
  REQUIRE(std::find_if(vectors.begin(), vectors.end(), [](const auto& item) {
            return item.id == "gpt-5.4-preview" && !item.accepted;
          }) != vectors.end());
  REQUIRE(std::find_if(vectors.begin(), vectors.end(), [](const auto& item) {
            return item.id == "gpt-5.40" && !item.accepted;
          }) != vectors.end());
  REQUIRE(std::find_if(vectors.begin(), vectors.end(), [](const auto& item) {
            return item.id == "gpt-5.10" && !item.accepted;
          }) != vectors.end());
}

TEST_CASE("request_header_helpers_assert_expected_headers") {
  HttpRequest request{.headers = {{"Authorization", "Bearer token"},
                                  {"ChatGPT-Account-Id", "acct-123"},
                                  {"session_id", DeterministicSessionId()}}};

  REQUIRE_NOTHROW(AssertHeaderEquals(request, "Authorization", "Bearer token"));
  REQUIRE_NOTHROW(
      AssertHeaderEquals(request, "ChatGPT-Account-Id", "acct-123"));
  REQUIRE_NOTHROW(
      AssertHeaderEquals(request, "session_id", DeterministicSessionId()));
  REQUIRE_NOTHROW(AssertHeaderAbsent(request, "OpenAI-Beta"));
  REQUIRE_THROWS_AS(AssertHeaderEquals(request, "Authorization", "Bearer bad"),
                    std::runtime_error);
  REQUIRE_THROWS_AS(AssertHeaderAbsent(request, "Authorization"),
                    std::runtime_error);
}

TEST_CASE("deterministic_session_and_clock_helpers_are_stable") {
  REQUIRE(DeterministicSessionId() == "00000000-0000-4000-8000-000000000001");
  REQUIRE(DeterministicTestNow().time_since_epoch() ==
          std::chrono::seconds{1'800'000'000});
}
