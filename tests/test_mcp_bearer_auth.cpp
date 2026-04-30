#include "mcp/mcp_server_config.hpp"
#include "mcp/mcp_transport.hpp"
#include "mcp/protocol_constants.hpp"
#include "mcp/streamable_http_mcp_transport.hpp"

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <functional>
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

namespace yac::mcp::test {
namespace {

namespace pc = protocol;

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

[[nodiscard]] std::string ToLower(std::string value) {
  for (char& ch : value) {
    ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
  }
  return value;
}

[[nodiscard]] std::string ReasonPhrase(int status) {
  switch (status) {
    case 200:
      return "OK";
    case 401:
      return "Unauthorized";
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

  [[nodiscard]] std::string Url() const {
    return "http://127.0.0.1:" + std::to_string(port_) + "/mcp";
  }

  [[nodiscard]] std::vector<HttpRequest> Requests() const {
    std::scoped_lock lock(mutex_);
    return requests_;
  }

 private:
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
        request.headers.emplace(ToLower(line.substr(0, colon_pos)),
                                Trim(line.substr(colon_pos + 1)));
      }
      if (line_end == std::string::npos) {
        break;
      }
      line_start = line_end + 2;
    }

    std::size_t content_length = 0;
    if (const auto it = request.headers.find("content-length");
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

    bool has_content_type = false;
    for (const auto& [key, value] : response.headers) {
      if (ToLower(key) == "content-type") {
        has_content_type = true;
      }
      wire += key + ": " + value + "\r\n";
    }
    if (!has_content_type) {
      wire += "Content-Type: application/json\r\n";
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

  Handler handler_;
  int listen_fd_ = -1;
  unsigned short port_ = 0;
  mutable std::mutex mutex_;
  std::vector<HttpRequest> requests_;
  std::atomic<bool> stop_{false};
  std::jthread worker_;
};

[[nodiscard]] McpServerConfig MakeConfig(const std::string& url,
                                         McpAuthBearer auth) {
  McpServerConfig cfg;
  cfg.id = "bearer-test";
  cfg.transport = "http";
  cfg.url = url;
  cfg.auth = std::move(auth);
  return cfg;
}

}  // namespace

TEST_CASE("header_attached") {
  ::setenv("YAC_TEST_TOKEN", "testval", 1);

  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = R"({"jsonrpc":"2.0","id":1,"result":{}})",
    };
  });

  StreamableHttpMcpTransport transport(
      MakeConfig(server.Url(), McpAuthBearer{.api_key_env = "YAC_TEST_TOKEN"}));
  transport.Start();

  REQUIRE(transport.Status() == TransportStatus::Ready);

  (void)transport.SendRequest(pc::kMethodPing, Json::object(), 2s,
                              std::stop_token{});

  const auto requests = server.Requests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].headers.contains("authorization"));
  REQUIRE(requests[0].headers.at("authorization") == "Bearer testval");

  ::unsetenv("YAC_TEST_TOKEN");
}

TEST_CASE("missing_env_graceful") {
  ::unsetenv("YAC_TEST_MISSING_TOKEN");

  McpServerConfig cfg;
  cfg.id = "bearer-test";
  cfg.transport = "http";
  cfg.url = "http://127.0.0.1:19999/mcp";
  cfg.auth = McpAuthBearer{.api_key_env = "YAC_TEST_MISSING_TOKEN"};

  StreamableHttpMcpTransport transport(std::move(cfg));
  transport.Start();

  REQUIRE(transport.Status() == TransportStatus::Failed);

  REQUIRE_THROWS_WITH(transport.SendRequest(pc::kMethodPing, Json::object(), 2s,
                                            std::stop_token{}),
                      ContainsSubstring("YAC_TEST_MISSING_TOKEN"));
}

}  // namespace yac::mcp::test
