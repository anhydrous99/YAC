#include <arpa/inet.h>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <sys/socket.h>
#include <unistd.h>

namespace {

[[nodiscard]] std::string Base64Url(const unsigned char* data, size_t len) {
  constexpr std::string_view kChars =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3) {
    const unsigned int b =
        (static_cast<unsigned int>(data[i]) << 16U) |
        (i + 1 < len ? static_cast<unsigned int>(data[i + 1]) << 8U : 0U) |
        (i + 2 < len ? static_cast<unsigned int>(data[i + 2]) : 0U);
    out += kChars[(b >> 18U) & 63U];
    out += kChars[(b >> 12U) & 63U];
    out += (i + 1 < len) ? kChars[(b >> 6U) & 63U] : '=';
    out += (i + 2 < len) ? kChars[b & 63U] : '=';
  }
  for (auto& c : out) {
    if (c == '+') {
      c = '-';
    } else if (c == '/') {
      c = '_';
    }
  }
  while (!out.empty() && out.back() == '=') {
    out.pop_back();
  }
  return out;
}

[[nodiscard]] std::string Sha256Base64Url(std::string_view input) {
  std::array<unsigned char, 32> digest{};
  unsigned int digest_len = 32;
  using EvpCtxPtr = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
  const EvpCtxPtr ctx{EVP_MD_CTX_new(), EVP_MD_CTX_free};
  EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr);
  EVP_DigestUpdate(ctx.get(), input.data(), input.size());
  EVP_DigestFinal_ex(ctx.get(), digest.data(), &digest_len);
  return Base64Url(digest.data(), digest_len);
}

[[nodiscard]] std::string UrlDecode(std::string_view s) {
  std::string out;
  for (size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '%' && i + 2 < s.size()) {
      const int hi =
          std::isdigit(static_cast<unsigned char>(s[i + 1])) != 0
              ? s[i + 1] - '0'
              : std::tolower(static_cast<unsigned char>(s[i + 1])) - 'a' + 10;
      const int lo =
          std::isdigit(static_cast<unsigned char>(s[i + 2])) != 0
              ? s[i + 2] - '0'
              : std::tolower(static_cast<unsigned char>(s[i + 2])) - 'a' + 10;
      out += static_cast<char>((hi << 4) | lo);
      i += 2;
    } else if (s[i] == '+') {
      out += ' ';
    } else {
      out += s[i];
    }
  }
  return out;
}

[[nodiscard]] std::map<std::string, std::string> ParseQuery(
    std::string_view query) {
  std::map<std::string, std::string> params;
  while (!query.empty()) {
    const size_t amp = query.find('&');
    const std::string_view pair =
        (amp == std::string_view::npos) ? query : query.substr(0, amp);
    const size_t eq = pair.find('=');
    if (eq != std::string_view::npos) {
      params[UrlDecode(pair.substr(0, eq))] = UrlDecode(pair.substr(eq + 1));
    }
    if (amp == std::string_view::npos) {
      break;
    }
    query = query.substr(amp + 1);
  }
  return params;
}

struct HttpRequest {
  std::string method;
  std::string path;
  std::map<std::string, std::string> query;
  std::map<std::string, std::string> headers;
  std::string body;
};

[[nodiscard]] std::optional<HttpRequest> ParseRequest(int fd) {
  std::string raw;
  std::array<char, 4096> buf{};
  while (raw.find("\r\n\r\n") == std::string::npos) {
    const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
      return std::nullopt;
    }
    raw.append(buf.data(), static_cast<size_t>(n));
    if (raw.size() > 65536U) {
      return std::nullopt;
    }
  }
  const size_t sep = raw.find("\r\n\r\n");
  std::string body_chunk = raw.substr(sep + 4);

  std::istringstream ss(raw.substr(0, sep));
  HttpRequest req;
  std::string line;
  if (!std::getline(ss, line)) {
    return std::nullopt;
  }
  if (!line.empty() && line.back() == '\r') {
    line.pop_back();
  }
  const size_t s1 = line.find(' ');
  const size_t s2 =
      (s1 == std::string::npos) ? std::string::npos : line.find(' ', s1 + 1);
  if (s1 == std::string::npos || s2 == std::string::npos) {
    return std::nullopt;
  }
  req.method = line.substr(0, s1);
  const std::string full_path = line.substr(s1 + 1, s2 - s1 - 1);
  const size_t qpos = full_path.find('?');
  if (qpos != std::string::npos) {
    req.path = full_path.substr(0, qpos);
    req.query = ParseQuery(full_path.substr(qpos + 1));
  } else {
    req.path = full_path;
  }

  while (std::getline(ss, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.empty()) {
      break;
    }
    const size_t colon = line.find(':');
    if (colon != std::string::npos) {
      std::string key = line.substr(0, colon);
      std::string val = line.substr(colon + 1);
      for (auto& c : key) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (!val.empty() && val.front() == ' ') {
        val = val.substr(1);
      }
      req.headers[std::move(key)] = std::move(val);
    }
  }

  size_t content_len = 0;
  if (req.headers.contains("content-length")) {
    content_len = std::stoul(req.headers.at("content-length"));
  }
  while (body_chunk.size() < content_len) {
    const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
      break;
    }
    body_chunk.append(buf.data(), static_cast<size_t>(n));
  }
  if (body_chunk.size() > content_len) {
    body_chunk.resize(content_len);
  }
  req.body = body_chunk;

  if (req.headers.contains("content-type") &&
      req.headers.at("content-type")
              .find("application/x-www-form-urlencoded") != std::string::npos) {
    auto form = ParseQuery(body_chunk);
    req.query.insert(form.begin(), form.end());
  }

  return req;
}

void SendResponse(int fd, int status, std::string_view status_text,
                  std::string_view content_type, std::string_view body,
                  const std::map<std::string, std::string>& extra = {}) {
  std::ostringstream resp;
  resp << "HTTP/1.1 " << status << " " << status_text << "\r\n";
  resp << "Content-Type: " << content_type << "\r\n";
  resp << "Content-Length: " << body.size() << "\r\n";
  for (const auto& [k, v] : extra) {
    resp << k << ": " << v << "\r\n";
  }
  resp << "Connection: close\r\n\r\n" << body;
  const std::string data = resp.str();
  ::send(fd, data.data(), data.size(), 0);
}

struct Options {
  int port = 8765;
  std::string inject_state;
  std::string canned_code = "test-code-12345";
  bool require_resource_param = false;
  std::optional<std::string> log_requests_to;
};

[[nodiscard]] bool TryConsumeFlag(std::string_view arg, std::string_view prefix,
                                  std::string_view& out) {
  if (!arg.starts_with(prefix)) {
    return false;
  }
  out = arg.substr(prefix.size());
  return true;
}

[[nodiscard]] Options ParseOptions(int argc, char** argv) {
  Options opts;
  std::string_view val;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (TryConsumeFlag(arg, "--port=", val)) {
      opts.port = std::atoi(std::string(val).c_str());
    } else if (TryConsumeFlag(arg, "--inject-state=", val)) {
      opts.inject_state = std::string(val);
    } else if (TryConsumeFlag(arg, "--canned-code=", val)) {
      opts.canned_code = std::string(val);
    } else if (TryConsumeFlag(arg, "--require-resource-param=", val)) {
      opts.require_resource_param = (val == "true");
    } else if (TryConsumeFlag(arg, "--log-requests-to=", val)) {
      opts.log_requests_to = std::string(val);
    }
  }
  return opts;
}

struct ServerState {
  std::map<std::string, std::string> pending_challenges;
};

void HandleRequest(int fd, const Options& opts, ServerState& srv_state,
                   std::ofstream* log_out) {
  auto maybe_req = ParseRequest(fd);
  if (!maybe_req) {
    return;
  }
  const HttpRequest& req = *maybe_req;

  if (log_out != nullptr) {
    *log_out << req.method << " " << req.path << "\n";
    log_out->flush();
  }

  const std::string base = "http://127.0.0.1:" + std::to_string(opts.port);

  if (req.path == "/.well-known/oauth-protected-resource") {
    SendResponse(fd, 200, "OK", "application/json",
                 R"({"resource":")" + base + R"(/mcp",")" +
                     R"(authorization_servers":[")" + base + R"("]})");
    return;
  }

  if (req.path == "/.well-known/oauth-authorization-server") {
    SendResponse(fd, 200, "OK", "application/json",
                 R"({"issuer":")" + base + R"(",)" +
                     R"("authorization_endpoint":")" + base +
                     R"(/authorize",)" + R"("token_endpoint":")" + base +
                     R"(/token",)" + R"("registration_endpoint":")" + base +
                     R"(/register",)" +
                     R"("code_challenge_methods_supported":["S256"]})");
    return;
  }

  if (req.path == "/authorize") {
    const auto& q = req.query;
    if (!q.contains("code_challenge") || q.at("code_challenge").empty()) {
      SendResponse(fd, 400, "Bad Request", "text/plain",
                   "missing code_challenge");
      return;
    }
    const std::string oauth_state = q.contains("state") ? q.at("state") : "";
    const std::string redir =
        q.contains("redirect_uri") ? q.at("redirect_uri") : "";
    srv_state.pending_challenges[opts.canned_code] = q.at("code_challenge");
    std::string location = redir + "?code=" + opts.canned_code;
    if (!oauth_state.empty()) {
      location += "&state=" + oauth_state;
    }
    SendResponse(fd, 302, "Found", "text/plain", "", {{"Location", location}});
    return;
  }

  if (req.path == "/token") {
    const auto& q = req.query;
    if (opts.require_resource_param && !q.contains("resource")) {
      SendResponse(
          fd, 400, "Bad Request", "application/json",
          R"({"error":"invalid_request","error_description":"resource required"})");
      return;
    }
    const std::string code = q.contains("code") ? q.at("code") : "";
    const std::string verifier =
        q.contains("code_verifier") ? q.at("code_verifier") : "";
    const auto it = srv_state.pending_challenges.find(code);
    if (it == srv_state.pending_challenges.end()) {
      SendResponse(fd, 400, "Bad Request", "application/json",
                   R"({"error":"invalid_grant"})");
      return;
    }
    if (Sha256Base64Url(verifier) != it->second) {
      SendResponse(
          fd, 400, "Bad Request", "application/json",
          R"({"error":"invalid_grant","error_description":"pkce mismatch"})");
      return;
    }
    srv_state.pending_challenges.erase(it);
    SendResponse(fd, 200, "OK", "application/json",
                 R"({"access_token":"mock-access-token",)"
                 R"("token_type":"Bearer","expires_in":3600})");
    return;
  }

  if (req.path == "/register") {
    SendResponse(fd, 200, "OK", "application/json",
                 R"({"client_id":"canned-client-id",)"
                 R"("client_secret":"canned-secret"})");
    return;
  }

  if (req.path == "/mcp") {
    if (!req.headers.contains("authorization")) {
      SendResponse(fd, 401, "Unauthorized", "application/json",
                   R"({"error":"unauthorized"})",
                   {{"WWW-Authenticate", R"(Bearer realm="mock")"}});
      return;
    }
    SendResponse(fd, 200, "OK", "application/json",
                 R"({"jsonrpc":"2.0","id":1,"result":{"tools":[)"
                 R"({"name":"echo","description":"Echo",)"
                 R"("inputSchema":{"type":"object","properties":{}}}]}})");
    return;
  }

  SendResponse(fd, 404, "Not Found", "text/plain", "not found");
}

[[nodiscard]] int RunServer(const Options& opts, std::ofstream* log_out) {
  ServerState srv_state;
  if (!opts.inject_state.empty()) {
    const size_t colon = opts.inject_state.find(':');
    if (colon != std::string::npos) {
      srv_state.pending_challenges[opts.inject_state.substr(0, colon)] =
          opts.inject_state.substr(colon + 1);
    }
  }

  const int server_fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (server_fd < 0) {
    std::cerr << "mock_oauth_server: socket failed\n";
    return 1;
  }

  const int reuse = 1;
  ::setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = htonl(INADDR_ANY);
  addr.sin_port = htons(static_cast<uint16_t>(opts.port));

  if (::bind(server_fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    std::cerr << "mock_oauth_server: bind failed on port " << opts.port << "\n";
    ::close(server_fd);
    return 1;
  }

  ::listen(server_fd, 16);

  std::cout << "mock_oauth_server ready on port " << opts.port << "\n";
  std::cout.flush();

  for (;;) {
    const int client_fd = ::accept(server_fd, nullptr, nullptr);
    if (client_fd < 0) {
      continue;
    }
    HandleRequest(client_fd, opts, srv_state, log_out);
    ::close(client_fd);
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options opts = ParseOptions(argc, argv);
    std::ofstream log_stream;
    std::ofstream* log_out = nullptr;
    if (opts.log_requests_to.has_value()) {
      log_stream.open(*opts.log_requests_to, std::ios::app);
      log_out = &log_stream;
    }
    return RunServer(opts, log_out);
  } catch (const std::exception& e) {
    std::cerr << "mock_oauth_server: fatal: " << e.what() << "\n";
    return 2;
  } catch (...) {
    std::cerr << "mock_oauth_server: fatal: unknown exception\n";
    return 2;
  }
}
