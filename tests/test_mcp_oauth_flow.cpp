#include "mcp/oauth/flow.hpp"

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <functional>
#include <future>
#include <mutex>
#include <netinet/in.h>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace std::chrono_literals;
using Catch::Matchers::ContainsSubstring;

namespace yac::mcp::oauth::test {
namespace {

struct HttpRequest {
  std::string method;
  std::string path;
  std::unordered_map<std::string, std::string> headers;
  std::string body;
};

struct HttpResponse {
  int status = 200;
  std::vector<std::pair<std::string, std::string>> headers;
  std::string body;
};

[[nodiscard]] std::string Trim(std::string value) {
  while (!value.empty() && (value.front() == ' ' || value.front() == '\t' ||
                            value.front() == '\r' || value.front() == '\n')) {
    value.erase(value.begin());
  }
  while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                            value.back() == '\r' || value.back() == '\n')) {
    value.pop_back();
  }
  return value;
}

[[nodiscard]] std::string ReasonPhrase(int status) {
  switch (status) {
    case 200:
      return "OK";
    default:
      return "Test";
  }
}

class TestHttpServer {
 public:
  using Handler = std::function<HttpResponse(const HttpRequest&, std::size_t)>;

  explicit TestHttpServer(Handler handler)
      : handler_(std::move(handler)),
        listen_fd_(socket(AF_INET, SOCK_STREAM, 0)) {
    if (listen_fd_ < 0) {
      throw std::runtime_error("socket failed");
    }

    int enable = 1;
    if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable,
                   sizeof(enable)) != 0) {
      close(listen_fd_);
      throw std::runtime_error("setsockopt failed");
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) !=
        0) {
      close(listen_fd_);
      throw std::runtime_error("bind failed");
    }
    if (listen(listen_fd_, 8) != 0) {
      close(listen_fd_);
      throw std::runtime_error("listen failed");
    }

    socklen_t len = sizeof(addr);
    if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) !=
        0) {
      close(listen_fd_);
      throw std::runtime_error("getsockname failed");
    }
    port_ = ntohs(addr.sin_port);

    worker_ =
        std::jthread([this](std::stop_token stop_token) { Run(stop_token); });
  }

  ~TestHttpServer() {
    stop_ = true;
    if (listen_fd_ >= 0) {
      shutdown(listen_fd_, SHUT_RDWR);
      close(listen_fd_);
      listen_fd_ = -1;
    }
  }

  TestHttpServer(const TestHttpServer&) = delete;
  TestHttpServer& operator=(const TestHttpServer&) = delete;
  TestHttpServer(TestHttpServer&&) = delete;
  TestHttpServer& operator=(TestHttpServer&&) = delete;

  [[nodiscard]] std::string Url(std::string_view path) const {
    return "http://127.0.0.1:" + std::to_string(port_) + std::string(path);
  }

  [[nodiscard]] std::vector<HttpRequest> Requests() const {
    std::scoped_lock lock(mutex_);
    return requests_;
  }

 private:
  [[nodiscard]] static HttpRequest ReadRequest(int client_fd) {
    std::string buffer;
    std::array<char, 1024> chunk{};
    std::size_t header_end = std::string::npos;
    while ((header_end = buffer.find("\r\n\r\n")) == std::string::npos) {
      const ssize_t bytes = recv(client_fd, chunk.data(), chunk.size(), 0);
      if (bytes <= 0) {
        throw std::runtime_error("recv header failed");
      }
      buffer.append(chunk.data(), static_cast<std::size_t>(bytes));
    }

    HttpRequest request;
    const std::string head = buffer.substr(0, header_end);
    std::size_t line_start = 0;
    std::size_t line_end = head.find("\r\n");
    const std::string request_line =
        line_end == std::string::npos ? head : head.substr(0, line_end);
    const std::size_t method_end = request_line.find(' ');
    const std::size_t path_end = request_line.find(' ', method_end + 1);
    request.method = request_line.substr(0, method_end);
    request.path =
        request_line.substr(method_end + 1, path_end - method_end - 1);

    line_start = line_end == std::string::npos ? head.size() : line_end + 2;
    while (line_start < head.size()) {
      line_end = head.find("\r\n", line_start);
      const std::string line = head.substr(
          line_start, line_end == std::string::npos ? std::string::npos
                                                    : line_end - line_start);
      const std::size_t colon_pos = line.find(':');
      if (colon_pos != std::string::npos) {
        request.headers.emplace(line.substr(0, colon_pos),
                                Trim(line.substr(colon_pos + 1)));
      }
      if (line_end == std::string::npos) {
        break;
      }
      line_start = line_end + 2;
    }

    std::size_t content_length = 0;
    if (const auto it = request.headers.find("Content-Length");
        it != request.headers.end()) {
      content_length = static_cast<std::size_t>(std::stoul(it->second));
    }

    request.body = buffer.substr(header_end + 4);
    while (request.body.size() < content_length) {
      const ssize_t bytes = recv(client_fd, chunk.data(), chunk.size(), 0);
      if (bytes <= 0) {
        throw std::runtime_error("recv body failed");
      }
      request.body.append(chunk.data(), static_cast<std::size_t>(bytes));
    }
    return request;
  }

  static void WriteResponse(int client_fd, const HttpResponse& response) {
    std::string wire = "HTTP/1.1 " + std::to_string(response.status) + " " +
                       ReasonPhrase(response.status) + "\r\n";
    for (const auto& [key, value] : response.headers) {
      wire += key + ": " + value + "\r\n";
    }
    wire += "Content-Length: " + std::to_string(response.body.size()) + "\r\n";
    wire += "Connection: close\r\n\r\n";
    wire += response.body;

    std::size_t written = 0;
    while (written < wire.size()) {
      const ssize_t bytes =
          send(client_fd, wire.data() + written, wire.size() - written, 0);
      if (bytes <= 0) {
        throw std::runtime_error("send failed");
      }
      written += static_cast<std::size_t>(bytes);
    }
  }

  void Run(std::stop_token stop_token) {
    while (!stop_.load() && !stop_token.stop_requested()) {
      sockaddr_in client_addr{};
      socklen_t client_len = sizeof(client_addr);
      const int client_fd = accept(
          listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
      if (client_fd < 0) {
        if (stop_.load() || stop_token.stop_requested()) {
          return;
        }
        continue;
      }

      try {
        HttpRequest request = ReadRequest(client_fd);
        const std::size_t request_index = [&] {
          std::scoped_lock lock(mutex_);
          requests_.push_back(request);
          return requests_.size() - 1;
        }();
        const HttpResponse response = handler_(request, request_index);
        WriteResponse(client_fd, response);
      } catch (const std::exception&) {
        // Mock server: drop malformed request, accept the next one.
      }

      close(client_fd);
    }
  }

  Handler handler_;
  int listen_fd_ = -1;
  unsigned short port_ = 0;
  mutable std::mutex mutex_;
  std::vector<HttpRequest> requests_;
  std::atomic<bool> stop_{false};
  std::jthread worker_;
};

}  // namespace

TEST_CASE("happy_path") {
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"access_token":"access-123","refresh_token":"refresh-456","token_type":"Bearer","scope":"alpha beta","expires_in":3600})",
    };
  });

  OAuthConfig config{.authorization_url = server.Url("/authorize"),
                     .token_url = server.Url("/token"),
                     .client_id = "client-123",
                     .scopes = {"alpha", "beta"},
                     .resource_url = "https://resource.example/api"};
  OAuthFlow flow;

  const std::string authorization_url = flow.BuildAuthorizationUrl(
      config, "challenge-value", "state-value", "http://127.0.0.1/callback",
      config.resource_url);
  flow.ValidateState("state-value");

  REQUIRE_THAT(authorization_url,
               ContainsSubstring("code_challenge=challenge-value"));
  REQUIRE_THAT(
      authorization_url,
      ContainsSubstring("resource=https%3A%2F%2Fresource.example%2Fapi"));
  REQUIRE_THAT(authorization_url, ContainsSubstring("scope=alpha%20beta"));

  const auto before = std::chrono::system_clock::now();
  const OAuthTokens tokens =
      flow.ExchangeCode(config, "code-xyz", "verifier-xyz",
                        "http://127.0.0.1/callback", config.resource_url);
  const auto after = std::chrono::system_clock::now();

  REQUIRE(tokens.access_token == "access-123");
  REQUIRE(tokens.refresh_token == "refresh-456");
  REQUIRE(tokens.token_type == "Bearer");
  REQUIRE(tokens.scope == "alpha beta");
  REQUIRE(tokens.expires_at >= before + 3599s);
  REQUIRE(tokens.expires_at <= after + 3601s);

  const auto requests = server.Requests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].method == "POST");
  REQUIRE(requests[0].path == "/token");
  REQUIRE_THAT(requests[0].body,
               ContainsSubstring("grant_type=authorization_code"));
  REQUIRE_THAT(requests[0].body, ContainsSubstring("code=code-xyz"));
  REQUIRE_THAT(requests[0].body,
               ContainsSubstring("code_verifier=verifier-xyz"));
  REQUIRE_THAT(
      requests[0].body,
      ContainsSubstring("resource=https%3A%2F%2Fresource.example%2Fapi"));
}

TEST_CASE("exchange_requires_state_validation") {
  OAuthFlow flow;
  OAuthConfig config{.authorization_url = "http://127.0.0.1/authorize",
                     .token_url = "http://127.0.0.1/token",
                     .client_id = "client-123",
                     .scopes = {"alpha"},
                     .resource_url = "https://resource.example/api"};

  (void)flow.BuildAuthorizationUrl(config, "challenge-value", "state-value",
                                   "http://127.0.0.1/callback",
                                   config.resource_url);
  REQUIRE_THROWS_WITH(
      flow.ExchangeCode(config, "code-xyz", "verifier-xyz",
                        "http://127.0.0.1/callback", config.resource_url),
      ContainsSubstring("requires state validation"));
  REQUIRE_THROWS_WITH(flow.ValidateState("wrong-state"),
                      ContainsSubstring("state mismatch"));
}

TEST_CASE("refresh_token_is_single_flight") {
  std::atomic<int> request_count{0};
  TestHttpServer server([&request_count](const HttpRequest&, std::size_t) {
    ++request_count;
    // SLEEP-RATIONALE: deliberate server delay ensures both concurrent callers observe single-flight
    std::this_thread::sleep_for(100ms);
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body =
            R"({"access_token":"access-refresh","refresh_token":"refresh-456","token_type":"Bearer","scope":"alpha beta","expires_in":3600})",
    };
  });

  OAuthConfig config{.authorization_url = server.Url("/authorize"),
                     .token_url = server.Url("/token"),
                     .client_id = "client-123",
                     .scopes = {"alpha", "beta"},
                     .resource_url = "https://resource.example/api"};
  OAuthFlow flow;

  auto call_refresh = [&flow, &config] {
    return flow.RefreshToken(config, "refresh-456", config.resource_url);
  };

  auto first = std::async(std::launch::async, call_refresh);
  auto second = std::async(std::launch::async, call_refresh);

  const OAuthTokens first_tokens = first.get();
  const OAuthTokens second_tokens = second.get();

  REQUIRE(first_tokens.access_token == "access-refresh");
  REQUIRE(second_tokens.access_token == "access-refresh");
  REQUIRE(request_count == 1);

  const auto requests = server.Requests();
  REQUIRE(requests.size() == 1);
  REQUIRE_THAT(requests[0].body, ContainsSubstring("grant_type=refresh_token"));
}

TEST_CASE("non_loopback_http_endpoints_are_rejected") {
  OAuthFlow flow;
  OAuthConfig config{.authorization_url = "http://example.com/authorize",
                     .token_url = "http://example.com/token",
                     .client_id = "client-123",
                     .scopes = {"alpha"},
                     .resource_url = "https://resource.example/api"};

  REQUIRE_THROWS_WITH(flow.BuildAuthorizationUrl(
                          config, "challenge-value", "state-value",
                          "http://127.0.0.1/callback", config.resource_url),
                      ContainsSubstring("authorization_url"));
}

}  // namespace yac::mcp::oauth::test
