#include "mcp/oauth/loopback_callback.hpp"

#include "mcp/oauth/browser_launcher.hpp"

#include <arpa/inet.h>
#include <array>
#include <iostream>
#include <netinet/in.h>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

namespace yac::mcp::oauth {
namespace {

[[nodiscard]] std::optional<std::string> ParseQueryParam(std::string_view query,
                                                         std::string_view key) {
  std::size_t pos = 0;
  while (pos <= query.size()) {
    const std::size_t amp = query.find('&', pos);
    const std::string_view segment =
        query.substr(pos, amp == std::string_view::npos ? std::string_view::npos
                                                        : amp - pos);
    if (segment.size() > key.size() && segment.substr(0, key.size()) == key &&
        segment[key.size()] == '=') {
      return std::string(segment.substr(key.size() + 1));
    }
    if (amp == std::string_view::npos) {
      break;
    }
    pos = amp + 1;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<std::pair<std::string, std::string>>
ParseCallbackUrl(std::string_view url) {
  const std::size_t q = url.find('?');
  if (q == std::string_view::npos) {
    return std::nullopt;
  }
  const std::string_view query = url.substr(q + 1);
  auto code = ParseQueryParam(query, "code");
  auto state = ParseQueryParam(query, "state");
  if (!code.has_value() || !state.has_value()) {
    return std::nullopt;
  }
  return std::make_pair(std::move(*code), std::move(*state));
}

[[nodiscard]] std::string ParseRequestPath(std::string_view request) {
  const std::size_t line_end = request.find("\r\n");
  const std::string_view first_line = request.substr(
      0, line_end == std::string_view::npos ? request.size() : line_end);
  const std::size_t sp1 = first_line.find(' ');
  if (sp1 == std::string_view::npos) {
    return {};
  }
  const std::size_t sp2 = first_line.find(' ', sp1 + 1);
  if (sp2 == std::string_view::npos) {
    return std::string(first_line.substr(sp1 + 1));
  }
  return std::string(first_line.substr(sp1 + 1, sp2 - sp1 - 1));
}

constexpr std::string_view kSuccessHtmlBody =
    "<html><body><h1>Authorization successful</h1>"
    "<p>You may close this tab.</p></body></html>";

[[nodiscard]] std::string BuildHttpResponse(int status, std::string_view reason,
                                            std::string_view body) {
  return std::string("HTTP/1.1 ") + std::to_string(status) + " " +
         std::string(reason) +
         "\r\nContent-Type: text/html; charset=utf-8\r\n"
         "Connection: close\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\n\r\n" + std::string(body);
}

}  // namespace

LoopbackCallbackServer::LoopbackCallbackServer()
    : listen_fd_(socket(AF_INET, SOCK_STREAM, 0)) {
  if (listen_fd_ < 0) {
    throw std::runtime_error("LoopbackCallbackServer: socket() failed");
  }

  int enable = 1;
  if (setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &enable,
                 sizeof(enable)) != 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    throw std::runtime_error("LoopbackCallbackServer: setsockopt() failed");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  addr.sin_port = 0;

  if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    throw std::runtime_error("LoopbackCallbackServer: bind() failed");
  }
  if (listen(listen_fd_, 1) != 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    throw std::runtime_error("LoopbackCallbackServer: listen() failed");
  }

  socklen_t len = sizeof(addr);
  if (getsockname(listen_fd_, reinterpret_cast<sockaddr*>(&addr), &len) != 0) {
    close(listen_fd_);
    listen_fd_ = -1;
    throw std::runtime_error("LoopbackCallbackServer: getsockname() failed");
  }
  port_ = ntohs(addr.sin_port);
}

LoopbackCallbackServer::~LoopbackCallbackServer() {
  if (listen_fd_ >= 0) {
    close(listen_fd_);
    listen_fd_ = -1;
  }
}

std::string LoopbackCallbackServer::RedirectUri() const {
  return "http://127.0.0.1:" + std::to_string(port_) + "/callback";
}

std::optional<std::pair<std::string, std::string>>
LoopbackCallbackServer::RunUntilCallback(std::stop_token stop_token) {
  while (!stop_token.stop_requested()) {
    if (listen_fd_ < 0) {
      return std::nullopt;
    }

    fd_set read_fds;
    FD_ZERO(&read_fds);
    FD_SET(listen_fd_, &read_fds);

    timeval timeout{};
    timeout.tv_sec = 0;
    timeout.tv_usec = 100000;

    const int ready =
        select(listen_fd_ + 1, &read_fds, nullptr, nullptr, &timeout);
    if (ready < 0) {
      return std::nullopt;
    }
    if (ready == 0) {
      continue;
    }

    sockaddr_in client_addr{};
    socklen_t client_len = sizeof(client_addr);
    const int client_fd = accept(
        listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
    if (client_fd < 0) {
      return std::nullopt;
    }

    std::string buffer;
    std::array<char, 4096> chunk{};
    while (buffer.find("\r\n\r\n") == std::string::npos) {
      const ssize_t bytes = recv(client_fd, chunk.data(), chunk.size(), 0);
      if (bytes <= 0) {
        break;
      }
      buffer.append(chunk.data(), static_cast<std::size_t>(bytes));
    }

    const std::string path = ParseRequestPath(buffer);
    auto result = ParseCallbackUrl("http://localhost" + path);

    if (result.has_value()) {
      const std::string response =
          BuildHttpResponse(200, "OK", kSuccessHtmlBody);
      send(client_fd, response.data(), response.size(), 0);
    } else {
      const std::string response =
          BuildHttpResponse(400, "Bad Request", "Invalid callback.\r\n");
      send(client_fd, response.data(), response.size(), 0);
    }
    close(client_fd);

    close(listen_fd_);
    listen_fd_ = -1;
    return result;
  }
  return std::nullopt;
}

std::optional<std::pair<std::string, std::string>> RunOAuthInteraction(
    const OAuthInteractionMode& mode, std::string_view auth_url,
    std::stop_token stop_token, std::istream& in) {
  if (mode.browser_disabled) {
    if (mode.injected_callback_url.has_value()) {
      return ParseCallbackUrl(*mode.injected_callback_url);
    }
    std::cout << "Open the following URL in your browser to authorize:\n"
              << auth_url << "\n\nPaste the callback URL here: " << std::flush;
    std::string line;
    if (!std::getline(in, line)) {
      return std::nullopt;
    }
    return ParseCallbackUrl(line);
  }

  LoopbackCallbackServer server;
  const std::string redirect_uri = server.RedirectUri();
  std::string full_url = std::string(auth_url);
  full_url += (full_url.find('?') == std::string::npos ? '?' : '&');
  full_url += "redirect_uri=" + redirect_uri;
  LaunchBrowser(full_url);
  return server.RunUntilCallback(std::move(stop_token));
}

}  // namespace yac::mcp::oauth
