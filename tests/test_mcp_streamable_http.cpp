#include "mcp/protocol_constants.hpp"
#include "mcp/streamable_http_mcp_transport.hpp"

#include <arpa/inet.h>
#include <atomic>
#include <cctype>
#include <chrono>
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

using namespace std::chrono_literals;

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
      }

      close(client_fd);
    }
  }

  [[nodiscard]] static HttpRequest ReadRequest(int client_fd) {
    std::string buffer;
    char chunk[1024];
    std::size_t header_end = std::string::npos;
    while ((header_end = buffer.find("\r\n\r\n")) == std::string::npos) {
      const ssize_t bytes = recv(client_fd, chunk, sizeof(chunk), 0);
      if (bytes <= 0) {
        throw std::runtime_error("recv header failed");
      }
      buffer.append(chunk, static_cast<std::size_t>(bytes));
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
      const ssize_t bytes = recv(client_fd, chunk, sizeof(chunk), 0);
      if (bytes <= 0) {
        throw std::runtime_error("recv body failed");
      }
      request.body.append(chunk, static_cast<std::size_t>(bytes));
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

[[nodiscard]] McpServerConfig MakeConfig(const std::string& url) {
  return McpServerConfig{.id = "http-test", .transport = "http", .url = url};
}

}  // namespace

TEST_CASE("json_response_path") {
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "application/json"},
                    {std::string(pc::kHeaderMcpSessionId), "session-json"}},
        .body =
            R"({"jsonrpc":"2.0","id":1,"result":{"ok":true,"mode":"json"}})",
    };
  });
  StreamableHttpMcpTransport transport(MakeConfig(server.Url()));

  transport.Start();
  const Json response = transport.SendRequest(pc::kMethodPing, Json::object(),
                                              2s, std::stop_token{});

  REQUIRE(transport.Status() == TransportStatus::Ready);
  REQUIRE(response[std::string(pc::kFieldId)] == 1);
  REQUIRE(response[std::string(pc::kFieldResult)]["ok"] == true);
  REQUIRE(response[std::string(pc::kFieldResult)]["mode"] == "json");

  const auto requests = server.Requests();
  REQUIRE(requests.size() == 1);
  REQUIRE(requests[0].headers.contains("accept"));
  REQUIRE(requests[0].headers.at("accept").find("application/json") !=
          std::string::npos);
  REQUIRE(requests[0].headers.at("accept").find("text/event-stream") !=
          std::string::npos);
  REQUIRE(requests[0].headers.at(
              ToLower(std::string(pc::kHeaderMcpProtocolVersion))) ==
          std::string(pc::kMcpProtocolVersion));
}

TEST_CASE("sse_response_path") {
  TestHttpServer server([](const HttpRequest&, std::size_t) {
    return HttpResponse{
        .headers = {{"Content-Type", "text/event-stream"},
                    {std::string(pc::kHeaderMcpSessionId), "session-sse"}},
        .body =
            "data: "
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/"
            "message\",\"params\":{\"step\":1}}\n\n"
            "data: "
            "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/"
            "progress\",\"params\":{\"progress\":0.5}}\n\n"
            "data: {\"jsonrpc\":\"2.0\",\n"
            "data: \"id\":1,\n"
            "data: \"result\":{\"ok\":true,\"mode\":\"sse\"}}\n\n",
    };
  });
  StreamableHttpMcpTransport transport(MakeConfig(server.Url()));
  std::vector<std::string> notification_methods;

  transport.SetNotificationCallback(
      [&notification_methods](std::string_view method, const Json& params) {
        notification_methods.emplace_back(method);
        REQUIRE(params.is_object());
      });

  transport.Start();
  const Json response = transport.SendRequest(pc::kMethodPing, Json::object(),
                                              2s, std::stop_token{});

  REQUIRE(
      notification_methods ==
      std::vector<std::string>{std::string(pc::kMethodNotificationsMessage),
                               std::string(pc::kMethodNotificationsProgress)});
  REQUIRE(response[std::string(pc::kFieldId)] == 1);
  REQUIRE(response[std::string(pc::kFieldResult)]["ok"] == true);
  REQUIRE(response[std::string(pc::kFieldResult)]["mode"] == "sse");
}

TEST_CASE("session_id_echo") {
  TestHttpServer server([](const HttpRequest& request, std::size_t index) {
    if (index == 0) {
      return HttpResponse{
          .headers = {{"Content-Type", "application/json"},
                      {std::string(pc::kHeaderMcpSessionId), "session-echo"}},
          .body = R"({"jsonrpc":"2.0","id":1,"result":{"ok":true}})",
      };
    }

    return HttpResponse{
        .headers = {{"Content-Type", "application/json"}},
        .body = R"({"jsonrpc":"2.0","id":2,"result":{"ok":true}})",
    };
  });
  StreamableHttpMcpTransport transport(MakeConfig(server.Url()));

  transport.Start();
  (void)transport.SendRequest(pc::kMethodPing, Json::object(), 2s,
                              std::stop_token{});
  const Json response = transport.SendRequest(pc::kMethodPing, Json::object(),
                                              2s, std::stop_token{});

  REQUIRE(response[std::string(pc::kFieldId)] == 2);
  const auto requests = server.Requests();
  REQUIRE(requests.size() == 2);
  const auto session_key = ToLower(std::string(pc::kHeaderMcpSessionId));
  REQUIRE(!requests[0].headers.contains(session_key));
  REQUIRE(requests[1].headers.contains(session_key));
  REQUIRE(requests[1].headers.at(session_key) == "session-echo");
}

}  // namespace yac::mcp::test
