#include "mcp/oauth/loopback_callback.hpp"

#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <future>
#include <netinet/in.h>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

namespace yac::mcp::oauth::test {

TEST_CASE("captures_code_state") {
  LoopbackCallbackServer server;
  const std::string redirect_uri = server.RedirectUri();

  std::optional<std::pair<std::string, std::string>> result;
  std::jthread server_thread([&](std::stop_token stop_token) {
    result = server.RunUntilCallback(std::move(stop_token));
  });

  const std::size_t port_start = redirect_uri.find("127.0.0.1:") + 10;
  const std::size_t port_end = redirect_uri.find('/', port_start);
  const auto port = static_cast<unsigned short>(
      std::stoul(redirect_uri.substr(port_start, port_end - port_start)));

  const int sock = socket(AF_INET, SOCK_STREAM, 0);
  REQUIRE(sock >= 0);

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = htons(port);
  REQUIRE(connect(sock, reinterpret_cast<const sockaddr*>(&addr),
                  sizeof(addr)) == 0);

  const std::string request =
      "GET /callback?code=abc&state=xyz HTTP/1.1\r\n"
      "Host: 127.0.0.1\r\n"
      "Connection: close\r\n\r\n";
  send(sock, request.data(), request.size(), 0);

  std::array<char, 256> buf{};
  while (recv(sock, buf.data(), buf.size(), 0) > 0) {
  }
  close(sock);

  server_thread.join();

  using Pair = std::pair<std::string, std::string>;
  REQUIRE(result == std::optional<Pair>(Pair{"abc", "xyz"}));
}

TEST_CASE("no_browser_injected_url") {
  OAuthInteractionMode mode{};
  mode.browser_disabled = true;
  mode.injected_callback_url =
      "http://127.0.0.1:0/callback?code=test&state=xyz";

  std::stop_source stop_source;
  std::istringstream unused_input;
  const auto result =
      RunOAuthInteraction(mode, "http://auth.example.com/authorize",
                          stop_source.get_token(), unused_input);

  using Pair = std::pair<std::string, std::string>;
  REQUIRE(result == std::optional<Pair>(Pair{"test", "xyz"}));
}

TEST_CASE("no_browser_stdin_paste") {
  OAuthInteractionMode mode{};
  mode.browser_disabled = true;

  std::istringstream input_stream(
      "http://127.0.0.1:0/callback?code=piped&state=s1");

  std::stop_source stop_source;
  const auto result =
      RunOAuthInteraction(mode, "http://auth.example.com/authorize",
                          stop_source.get_token(), input_stream);

  using Pair = std::pair<std::string, std::string>;
  REQUIRE(result == std::optional<Pair>(Pair{"piped", "s1"}));
}

TEST_CASE("abort_accept") {
  LoopbackCallbackServer server;
  std::stop_source stop_source;

  auto future = std::async(std::launch::async, [&] {
    return server.RunUntilCallback(stop_source.get_token());
  });

  // SLEEP-RATIONALE: let server reach accept() before requesting stop; no observable predicate
  std::this_thread::sleep_for(50ms);
  stop_source.request_stop();

  const auto status = future.wait_for(500ms);
  REQUIRE(status == std::future_status::ready);
  REQUIRE_FALSE(future.get().has_value());
}

}  // namespace yac::mcp::oauth::test
