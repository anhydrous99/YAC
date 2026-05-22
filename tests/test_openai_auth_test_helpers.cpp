#include "openai_auth_test_helpers.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <sys/socket.h>
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

  const auto port_start = url.find(':', url.find("127.0.0.1")) + 1;
  const auto port_end = url.find('/', port_start);
  const auto port = static_cast<unsigned short>(
      std::stoul(url.substr(port_start, port_end - port_start)));

  const int sock = socket(AF_INET, SOCK_STREAM, 0);
  REQUIRE(sock >= 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  REQUIRE(connect(sock, reinterpret_cast<const sockaddr*>(&addr),
                  sizeof(addr)) == 0);
  REQUIRE(send(sock, request.data(), request.size(), 0) ==
          static_cast<ssize_t>(request.size()));

  std::array<char, 64> buffer{};
  REQUIRE(recv(sock, buffer.data(), buffer.size(), 0) > 0);
  close(sock);

  const auto requests = server.Requests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].path == "/token");
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
