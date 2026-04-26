#include "mcp/oauth/metadata_discovery.hpp"

#include <arpa/inet.h>
#include <atomic>
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

[[nodiscard]] std::string TrimWhitespace(std::string value) {
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
    case 404:
      return "Not Found";
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
    std::lock_guard lock(mutex_);
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
                                TrimWhitespace(line.substr(colon_pos + 1)));
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
          std::lock_guard lock(mutex_);
          requests_.push_back(request);
          return requests_.size() - 1;
        }();
        const HttpResponse response = handler_(request, request_index);
        WriteResponse(client_fd, response);
      } catch (const std::exception&) {
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

constexpr std::string_view kAsMetadataWithS256 =
    R"({"authorization_endpoint":"https://as.example.com/authorize",)"
    R"("token_endpoint":"https://as.example.com/token",)"
    R"("code_challenge_methods_supported":["S256"]})";

constexpr std::string_view kAsMetadataNoS256 =
    R"({"authorization_endpoint":"https://as.example.com/authorize",)"
    R"("token_endpoint":"https://as.example.com/token",)"
    R"("code_challenge_methods_supported":["plain"]})";

}  // namespace

TEST_CASE("uses_www_authenticate") {
  std::string server_base_url;

  TestHttpServer server(
      [&server_base_url](const HttpRequest& req, std::size_t) {
        if (req.path == "/resource-metadata") {
          const std::string body =
              "{\"authorization_servers\":[\"" + server_base_url + "\"]}";
          return HttpResponse{
              .headers = {{"Content-Type", "application/json"}},
              .body = body,
          };
        }
        if (req.path == "/.well-known/oauth-authorization-server") {
          return HttpResponse{
              .headers = {{"Content-Type", "application/json"}},
              .body = std::string(kAsMetadataWithS256),
          };
        }
        return HttpResponse{.status = 404, .body = ""};
      });

  server_base_url = server.Url("");

  const std::string www_auth =
      "Bearer resource_metadata=\"" + server.Url("/resource-metadata") + "\"";

  const auto result =
      DiscoverProtectedResource("http://not-used.example.com", www_auth);

  REQUIRE(result.authorization_endpoint == "https://as.example.com/authorize");
  REQUIRE(result.token_endpoint == "https://as.example.com/token");

  const auto requests = server.Requests();
  bool hit_resource_metadata = false;
  bool hit_well_known_fallback = false;
  for (const auto& req : requests) {
    if (req.path == "/resource-metadata") {
      hit_resource_metadata = true;
    }
    if (req.path == "/.well-known/oauth-protected-resource") {
      hit_well_known_fallback = true;
    }
  }
  REQUIRE(hit_resource_metadata);
  REQUIRE_FALSE(hit_well_known_fallback);
}

TEST_CASE("refuses_missing_pkce") {
  TestHttpServer server([](const HttpRequest& req, std::size_t) {
    if (req.path == "/.well-known/oauth-authorization-server") {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"}},
          .body = std::string(kAsMetadataNoS256),
      };
    }
    return HttpResponse{.status = 404, .body = ""};
  });

  REQUIRE_THROWS_WITH(DiscoverAuthorizationServer(server.Url("")),
                      ContainsSubstring("PKCE S256"));
}

TEST_CASE("fallback_chain") {
  std::string server_base_url;

  TestHttpServer server(
      [&server_base_url](const HttpRequest& req, std::size_t) {
        if (req.path == "/.well-known/oauth-protected-resource") {
          const std::string body =
              "{\"authorization_servers\":[\"" + server_base_url + "\"]}";
          return HttpResponse{
              .headers = {{"Content-Type", "application/json"}},
              .body = body,
          };
        }
        if (req.path == "/.well-known/oauth-authorization-server") {
          return HttpResponse{
              .headers = {{"Content-Type", "application/json"}},
              .body = std::string(kAsMetadataWithS256),
          };
        }
        return HttpResponse{.status = 404, .body = ""};
      });

  server_base_url = server.Url("");

  const auto result = DiscoverProtectedResource(server.Url(""), "");

  REQUIRE(result.authorization_endpoint == "https://as.example.com/authorize");
  REQUIRE(result.token_endpoint == "https://as.example.com/token");
  REQUIRE_FALSE(result.code_challenge_methods_supported.empty());

  const auto requests = server.Requests();
  bool hit_well_known = false;
  for (const auto& req : requests) {
    if (req.path == "/.well-known/oauth-protected-resource") {
      hit_well_known = true;
    }
  }
  REQUIRE(hit_well_known);
}

}  // namespace yac::mcp::oauth::test
