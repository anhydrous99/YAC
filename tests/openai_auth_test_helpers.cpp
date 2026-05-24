#include "openai_auth_test_helpers.hpp"

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdexcept>
#include <string_view>
#include <sys/select.h>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <utility>

namespace yac::tests::openai_auth {

namespace {

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
    case 204:
      return "No Content";
    case 302:
      return "Found";
    default:
      return "Test";
  }
}

[[nodiscard]] std::string Base64UrlEncode(std::string_view input) {
  static constexpr std::string_view kAlphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

  std::string output;
  output.reserve(((input.size() + 2) / 3) * 4);

  unsigned int buffer = 0;
  int bits = 0;
  for (unsigned char c : input) {
    buffer = (buffer << 8) | c;
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      output += kAlphabet[(buffer >> bits) & 0x3f];
    }
  }
  if (bits > 0) {
    output += kAlphabet[(buffer << (6 - bits)) & 0x3f];
  }
  return output;
}

[[nodiscard]] std::string EscapeJsonString(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());
  for (char c : value) {
    switch (c) {
      case '\\':
        escaped += "\\\\";
        break;
      case '"':
        escaped += "\\\"";
        break;
      case '\n':
        escaped += "\\n";
        break;
      case '\r':
        escaped += "\\r";
        break;
      case '\t':
        escaped += "\\t";
        break;
      default:
        escaped += c;
        break;
    }
  }
  return escaped;
}

void WaitForReadable(int fd, std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    timeval timeout{};
    timeout.tv_usec = 10'000;

    const int ready = select(fd + 1, &read_fds, nullptr, nullptr, &timeout);
    if (ready < 0) {
      throw std::runtime_error("select failed");
    }
    if (ready > 0 && FD_ISSET(fd, &read_fds)) {
      return;
    }
  }
  throw std::runtime_error("request read stopped");
}

void WaitForWritable(int fd, std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    fd_set write_fds;
    FD_ZERO(&write_fds);
    FD_SET(fd, &write_fds);

    timeval timeout{};
    timeout.tv_usec = 10'000;

    const int ready = select(fd + 1, nullptr, &write_fds, nullptr, &timeout);
    if (ready < 0) {
      throw std::runtime_error("select failed");
    }
    if (ready > 0 && FD_ISSET(fd, &write_fds)) {
      return;
    }
  }
  throw std::runtime_error("response write stopped");
}

void ConfigureAcceptedClient(int client_fd) {
  const int flags = fcntl(client_fd, F_GETFL, 0);
  if (flags >= 0) {
    fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);
  }
#ifdef SO_NOSIGPIPE
  int enabled = 1;
  setsockopt(client_fd, SOL_SOCKET, SO_NOSIGPIPE, &enabled, sizeof(enabled));
#endif
}

int SendFlags() {
  int flags = MSG_DONTWAIT;
#ifdef MSG_NOSIGNAL
  flags |= MSG_NOSIGNAL;
#endif
  return flags;
}

}  // namespace

class TestHttpServer::Impl {
 public:
  explicit Impl(Handler in_handler) : handler_(std::move(in_handler)) {}

 private:
  friend class TestHttpServer;

  Handler handler_;
  int listen_fd_ = -1;
  unsigned short port_ = 0;
  mutable std::mutex mutex_;
  std::vector<HttpRequest> requests_;
  std::atomic<bool> stop_{false};
  std::jthread worker_;
};

TestHttpServer::TestHttpServer(Handler handler)
    : impl_(std::make_unique<Impl>(std::move(handler))) {
  impl_->listen_fd_ = socket(AF_INET, SOCK_STREAM, 0);
  if (impl_->listen_fd_ < 0) {
    throw std::runtime_error("socket failed");
  }

  int enable = 1;
  if (setsockopt(impl_->listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(enable)) != 0) {
    close(impl_->listen_fd_);
    throw std::runtime_error("setsockopt failed");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;
  if (bind(impl_->listen_fd_, reinterpret_cast<sockaddr*>(&addr),
           sizeof(addr)) != 0) {
    close(impl_->listen_fd_);
    throw std::runtime_error("bind failed");
  }
  if (listen(impl_->listen_fd_, 8) != 0) {
    close(impl_->listen_fd_);
    throw std::runtime_error("listen failed");
  }

  socklen_t len = sizeof(addr);
  if (getsockname(impl_->listen_fd_, reinterpret_cast<sockaddr*>(&addr),
                  &len) != 0) {
    close(impl_->listen_fd_);
    throw std::runtime_error("getsockname failed");
  }
  impl_->port_ = ntohs(addr.sin_port);

  impl_->worker_ = std::jthread(
      [impl = impl_.get()](std::stop_token stop_token) {
        Run(impl, stop_token);
      });
}

TestHttpServer::~TestHttpServer() {
  impl_->stop_ = true;
  impl_->worker_.request_stop();
  if (impl_->worker_.joinable()) {
    impl_->worker_.join();
  }
  if (impl_->listen_fd_ >= 0) {
    shutdown(impl_->listen_fd_, SHUT_RDWR);
    close(impl_->listen_fd_);
    impl_->listen_fd_ = -1;
  }
}

std::string TestHttpServer::Url(std::string_view path) const {
  return "http://127.0.0.1:" + std::to_string(impl_->port_) + std::string(path);
}

std::vector<HttpRequest> TestHttpServer::Requests() const {
  std::scoped_lock lock(impl_->mutex_);
  return impl_->requests_;
}

HttpRequest TestHttpServer::ReadRequest(int client_fd,
                                        std::stop_token stop_token) {
  std::string buffer;
  std::array<char, 1024> chunk{};
  std::size_t header_end = std::string::npos;
  while ((header_end = buffer.find("\r\n\r\n")) == std::string::npos) {
    WaitForReadable(client_fd, stop_token);
    const ssize_t bytes = recv(client_fd, chunk.data(), chunk.size(), 0);
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }
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
  request.path = request_line.substr(method_end + 1, path_end - method_end - 1);

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
    WaitForReadable(client_fd, stop_token);
    const ssize_t bytes = recv(client_fd, chunk.data(), chunk.size(), 0);
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }
    if (bytes <= 0) {
      throw std::runtime_error("recv body failed");
    }
    request.body.append(chunk.data(), static_cast<std::size_t>(bytes));
  }
  return request;
}

void TestHttpServer::WriteResponse(int client_fd, const HttpResponse& response,
                                   std::stop_token stop_token) {
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
    WaitForWritable(client_fd, stop_token);
    const ssize_t bytes = send(client_fd, wire.data() + written,
                               wire.size() - written, SendFlags());
    if (bytes < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
      continue;
    }
    if (bytes <= 0) {
      throw std::runtime_error("send failed");
    }
    written += static_cast<std::size_t>(bytes);
  }
}

void TestHttpServer::Run(Impl* impl, std::stop_token stop_token) {
  const int listen_fd = impl->listen_fd_;
  while (!impl->stop_.load() && !stop_token.stop_requested()) {
    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listen_fd, &read_fds);

    timeval timeout{};
    timeout.tv_usec = 10'000;

    const int ready = select(listen_fd + 1, &read_fds, nullptr, nullptr,
                             &timeout);
    if (ready <= 0) {
      continue;
    }

    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd =
        accept(listen_fd, reinterpret_cast<sockaddr*>(&client_addr),
               &client_len);
    if (client_fd < 0) {
      if (impl->stop_.load() || stop_token.stop_requested()) {
        return;
      }
      continue;
    }
    ConfigureAcceptedClient(client_fd);

    try {
      HttpRequest request = ReadRequest(client_fd, stop_token);
      const std::size_t request_index = [&] {
        std::scoped_lock lock(impl->mutex_);
        impl->requests_.push_back(request);
        return impl->requests_.size() - 1;
      }();
      const HttpResponse response = impl->handler_(request, request_index);
      WriteResponse(client_fd, response, stop_token);
    } catch (const std::exception&) {
      close(client_fd);
      continue;
    }

    close(client_fd);
  }
}

bool FakeBrowserLauncher::LaunchBrowser(std::string_view url) {
  std::scoped_lock lock(mutex_);
  urls_.emplace_back(url);
  return true;
}

std::vector<std::string> FakeBrowserLauncher::Urls() const {
  std::scoped_lock lock(mutex_);
  return urls_;
}

std::optional<std::string> FakeTokenStore::Get(
    std::string_view server_id) const {
  std::scoped_lock lock(mutex_);
  const auto it = tokens_.find(std::string(server_id));
  if (it == tokens_.end()) {
    return std::nullopt;
  }
  return it->second;
}

void FakeTokenStore::Set(std::string_view server_id,
                         std::string_view token_json) {
  std::scoped_lock lock(mutex_);
  tokens_[std::string(server_id)] = std::string(token_json);
}

void FakeTokenStore::Erase(std::string_view server_id) {
  std::scoped_lock lock(mutex_);
  tokens_.erase(std::string(server_id));
}

std::string MakeUnsignedJwtLikeToken(std::string_view payload_json) {
  const std::string header = Base64UrlEncode(R"({"alg":"none","typ":"JWT"})");
  const std::string payload = Base64UrlEncode(payload_json);
  return header + "." + payload + ".";
}

std::string MakeAccountIdJwtLikeToken(std::string_view account_id) {
  constexpr std::string_view kPrefix = R"({"chatgpt_account_id":")";
  constexpr std::string_view kSuffix = R"("})";
  const std::string payload = std::string(kPrefix) +
                              EscapeJsonString(account_id) +
                              std::string(kSuffix);
  return MakeUnsignedJwtLikeToken(payload);
}

std::string MakeNestedAccountIdJwtLikeToken(std::string_view account_id) {
  constexpr std::string_view kPrefix =
      R"({"https://api.openai.com/auth":{"chatgpt_account_id":")";
  constexpr std::string_view kSuffix = R"("}})";
  const std::string payload = std::string(kPrefix) +
                              EscapeJsonString(account_id) +
                              std::string(kSuffix);
  return MakeUnsignedJwtLikeToken(payload);
}

std::string MakeFlattenedAccountIdJwtLikeToken(std::string_view account_id) {
  constexpr std::string_view kPrefix =
      R"({"https://api.openai.com/auth.chatgpt_account_id":")";
  constexpr std::string_view kSuffix = R"("})";
  const std::string payload = std::string(kPrefix) +
                              EscapeJsonString(account_id) +
                              std::string(kSuffix);
  return MakeUnsignedJwtLikeToken(payload);
}

std::string MakeOrganizationFallbackJwtLikeToken(
    std::string_view organization_id) {
  constexpr std::string_view kPrefix = R"({"organizations":[{"id":")";
  constexpr std::string_view kSuffix = R"("}]})";
  const std::string payload = std::string(kPrefix) +
                              EscapeJsonString(organization_id) +
                              std::string(kSuffix);
  return MakeUnsignedJwtLikeToken(payload);
}

std::vector<std::string> StaticAcceptedModelIds() {
  return {"gpt-5.5", "gpt-5.2",     "gpt-5.3-codex", "gpt-5.3-codex-spark",
          "gpt-5.4", "gpt-5.4-mini"};
}

std::vector<ModelIdTestVector> DynamicModelIdTestVectors() {
  return {{.id = "gpt-5.5", .accepted = true},
          {.id = "gpt-5.41", .accepted = true},
          {.id = "gpt-6.0", .accepted = true},
          {.id = "gpt-5.4-preview", .accepted = false},
          {.id = "gpt-5.40", .accepted = false},
          {.id = "gpt-5.10", .accepted = false},
          {.id = "gpt-5", .accepted = false},
          {.id = "gpt-five.five", .accepted = false}};
}

std::string DeterministicSessionId() {
  return "00000000-0000-4000-8000-000000000001";
}

std::chrono::system_clock::time_point DeterministicTestNow() {
  return std::chrono::system_clock::time_point{
      std::chrono::seconds{1'800'000'000}};
}

void AssertHeaderEquals(const HttpRequest& request, std::string_view name,
                        std::string_view expected_value) {
  const auto it = request.headers.find(std::string(name));
  if (it == request.headers.end()) {
    throw std::runtime_error("missing expected request header: " +
                             std::string(name));
  }
  if (it->second != expected_value) {
    throw std::runtime_error("unexpected request header value for: " +
                             std::string(name));
  }
}

void AssertHeaderAbsent(const HttpRequest& request, std::string_view name) {
  if (request.headers.contains(std::string(name))) {
    throw std::runtime_error("unexpected request header present: " +
                             std::string(name));
  }
}

}  // namespace yac::tests::openai_auth
