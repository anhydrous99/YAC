#include "openai_auth_test_helpers.hpp"

#include <arpa/inet.h>
#include <array>
#include <atomic>
#include <netinet/in.h>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>

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
  if (bind(impl_->listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(impl_->listen_fd_);
    throw std::runtime_error("bind failed");
  }
  if (listen(impl_->listen_fd_, 8) != 0) {
    close(impl_->listen_fd_);
    throw std::runtime_error("listen failed");
  }

  socklen_t len = sizeof(addr);
  if (getsockname(impl_->listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    close(impl_->listen_fd_);
    throw std::runtime_error("getsockname failed");
  }
  impl_->port_ = ntohs(addr.sin_port);

  impl_->worker_ = std::jthread([this](std::stop_token stop_token) {
    Run(stop_token);
  });
}

TestHttpServer::~TestHttpServer() {
  impl_->stop_ = true;
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

HttpRequest TestHttpServer::ReadRequest(int client_fd) {
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
  request.path = request_line.substr(method_end + 1, path_end - method_end - 1);

  line_start = line_end == std::string::npos ? head.size() : line_end + 2;
  while (line_start < head.size()) {
    line_end = head.find("\r\n", line_start);
    const std::string line = head.substr(
        line_start,
        line_end == std::string::npos ? std::string::npos : line_end - line_start);
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

void TestHttpServer::WriteResponse(int client_fd, const HttpResponse& response) {
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

void TestHttpServer::Run(std::stop_token stop_token) {
  while (!impl_->stop_.load() && !stop_token.stop_requested()) {
    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd =
        accept(impl_->listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
      if (impl_->stop_.load() || stop_token.stop_requested()) {
        return;
      }
      continue;
    }

    try {
      HttpRequest request = ReadRequest(client_fd);
      const std::size_t request_index = [&] {
        std::scoped_lock lock(impl_->mutex_);
        impl_->requests_.push_back(request);
        return impl_->requests_.size() - 1;
      }();
      const HttpResponse response = impl_->handler_(request, request_index);
      WriteResponse(client_fd, response);
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

}  // namespace yac::tests::openai_auth
