#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <csignal>
#include <netinet/in.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace yac::test {
namespace {

using namespace std::chrono_literals;

constexpr int kTestPort = 18877;
constexpr const char* kCannedCode = "testcode123";

struct ProcessHandle {
  pid_t pid = -1;
  int read_fd = -1;
};

[[nodiscard]] ProcessHandle SpawnServer(
    const std::vector<std::string>& extra_args) {
  std::array<int, 2> out_pipe{};
  if (::pipe(out_pipe.data()) != 0) {
    throw std::runtime_error("pipe failed");
  }
  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    throw std::runtime_error("fork failed");
  }
  if (pid == 0) {
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    std::vector<std::string> argv_storage;
    argv_storage.emplace_back(MOCK_OAUTH_SERVER_PATH);
    argv_storage.push_back("--port=" + std::to_string(kTestPort));
    argv_storage.push_back("--canned-code=" + std::string(kCannedCode));
    for (const auto& a : extra_args) {
      argv_storage.push_back(a);
    }
    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto& s : argv_storage) {
      argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }
  ::close(out_pipe[1]);
  return ProcessHandle{.pid = pid, .read_fd = out_pipe[0]};
}

void WaitForReady(int fd) {
  const auto deadline = std::chrono::steady_clock::now() + 5s;
  std::string line;
  while (std::chrono::steady_clock::now() < deadline) {
    char ch = '\0';
    const ssize_t n = ::read(fd, &ch, 1);
    if (n == 1) {
      if (ch == '\n') {
        if (line.find("ready") != std::string::npos) {
          return;
        }
        line.clear();
      } else {
        line += ch;
      }
    } else {
      // SLEEP-RATIONALE: polls server-readiness without busy-waiting
      std::this_thread::sleep_for(5ms);
    }
  }
  throw std::runtime_error("server did not become ready");
}

void Cleanup(ProcessHandle& handle) {
  if (handle.read_fd >= 0) {
    ::close(handle.read_fd);
    handle.read_fd = -1;
  }
  if (handle.pid > 0) {
    int status = 0;
    if (::waitpid(handle.pid, &status, WNOHANG) == 0) {
      ::kill(handle.pid, SIGKILL);
      ::waitpid(handle.pid, &status, 0);
    }
    handle.pid = -1;
  }
}

struct HttpResponse {
  int status = 0;
  std::string body;
};

[[nodiscard]] int ConnectTo(int port) {
  const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0) {
    throw std::runtime_error("socket failed");
  }
  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(static_cast<uint16_t>(port));
  ::inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
    ::close(fd);
    throw std::runtime_error("connect failed");
  }
  return fd;
}

[[nodiscard]] HttpResponse ReadHttpResponse(int fd) {
  std::string resp;
  std::array<char, 4096> buf{};
  for (;;) {
    const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
      break;
    }
    resp.append(buf.data(), static_cast<size_t>(n));
  }
  HttpResponse result;
  const size_t sp = resp.find(' ');
  if (sp != std::string::npos && sp + 3 < resp.size()) {
    result.status = std::stoi(resp.substr(sp + 1, 3));
  }
  const size_t sep = resp.find("\r\n\r\n");
  if (sep != std::string::npos) {
    result.body = resp.substr(sep + 4);
  }
  return result;
}

[[nodiscard]] HttpResponse HttpGet(int port, const std::string& path) {
  const int fd = ConnectTo(port);
  const std::string req =
      "GET " + path +
      " HTTP/1.0\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n";
  ::send(fd, req.data(), req.size(), 0);
  auto result = ReadHttpResponse(fd);
  ::close(fd);
  return result;
}

[[nodiscard]] HttpResponse HttpPost(int port, const std::string& path,
                                    const std::string& body) {
  const int fd = ConnectTo(port);
  const std::string req = "POST " + path +
                          " HTTP/1.0\r\n"
                          "Host: 127.0.0.1\r\n"
                          "Content-Type: application/x-www-form-urlencoded\r\n"
                          "Content-Length: " +
                          std::to_string(body.size()) +
                          "\r\n"
                          "Connection: close\r\n\r\n" +
                          body;
  ::send(fd, req.data(), req.size(), 0);
  auto result = ReadHttpResponse(fd);
  ::close(fd);
  return result;
}

}  // namespace

TEST_CASE("discovery_endpoint_returns_metadata") {
  ProcessHandle handle = SpawnServer({});
  WaitForReady(handle.read_fd);

  const HttpResponse resp =
      HttpGet(kTestPort, "/.well-known/oauth-authorization-server");
  REQUIRE(resp.status == 200);
  REQUIRE(resp.body.find("authorization_endpoint") != std::string::npos);
  REQUIRE(resp.body.find("token_endpoint") != std::string::npos);
  REQUIRE(resp.body.find("registration_endpoint") != std::string::npos);
  REQUIRE(resp.body.find("S256") != std::string::npos);

  Cleanup(handle);
}

TEST_CASE("pkce_mismatch_returns_400") {
  ProcessHandle handle = SpawnServer({});
  WaitForReady(handle.read_fd);

  const HttpResponse auth_resp =
      HttpGet(kTestPort,
              "/authorize?code_challenge=FAKECHALLENGE"
              "&redirect_uri=http%3A%2F%2Flocalhost%2Fcb&state=s1");
  REQUIRE(auth_resp.status == 302);

  const std::string token_body =
      "code=" + std::string(kCannedCode) +
      "&code_verifier=wrong_verifier&grant_type=authorization_code";
  const HttpResponse token_resp = HttpPost(kTestPort, "/token", token_body);
  REQUIRE(token_resp.status == 400);

  Cleanup(handle);
}

}  // namespace yac::test
