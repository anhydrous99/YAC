#include <arpa/inet.h>
#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <netinet/in.h>
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

constexpr int kOAuthPort = 19877;
constexpr const char* kCannedCode = "test_code";

struct TempDir {
  std::filesystem::path path;
  TempDir() {
    std::string tmpl = "/tmp/yac_oauth_e2e_XXXXXX";
    const char* result = ::mkdtemp(tmpl.data());
    if (result == nullptr) {
      throw std::runtime_error("mkdtemp failed");
    }
    path = result;
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;
};

struct ProcessHandle {
  pid_t pid = -1;
  int read_fd = -1;

  ProcessHandle() = default;
  ProcessHandle(pid_t p, int fd) : pid(p), read_fd(fd) {}

  ~ProcessHandle() { Release(); }

  ProcessHandle(const ProcessHandle&) = delete;
  ProcessHandle& operator=(const ProcessHandle&) = delete;
  ProcessHandle(ProcessHandle&& other) noexcept
      : pid(other.pid), read_fd(other.read_fd) {
    other.pid = -1;
    other.read_fd = -1;
  }
  ProcessHandle& operator=(ProcessHandle&&) = delete;

  void Release() {
    if (read_fd >= 0) {
      ::close(read_fd);
      read_fd = -1;
    }
    if (pid > 0) {
      int status = 0;
      if (::waitpid(pid, &status, WNOHANG) == 0) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
      }
      pid = -1;
    }
  }
};

struct YacAuthHandle {
  pid_t pid = -1;
  int stdin_fd = -1;
  int stdout_fd = -1;

  YacAuthHandle() = default;

  ~YacAuthHandle() {
    if (stdin_fd >= 0) {
      ::close(stdin_fd);
      stdin_fd = -1;
    }
    if (stdout_fd >= 0) {
      ::close(stdout_fd);
      stdout_fd = -1;
    }
    if (pid > 0) {
      int status = 0;
      if (::waitpid(pid, &status, WNOHANG) == 0) {
        ::kill(pid, SIGKILL);
        ::waitpid(pid, &status, 0);
      }
      pid = -1;
    }
  }

  YacAuthHandle(const YacAuthHandle&) = delete;
  YacAuthHandle& operator=(const YacAuthHandle&) = delete;
  YacAuthHandle(YacAuthHandle&& other) noexcept
      : pid(other.pid), stdin_fd(other.stdin_fd), stdout_fd(other.stdout_fd) {
    other.pid = -1;
    other.stdin_fd = -1;
    other.stdout_fd = -1;
  }
  YacAuthHandle& operator=(YacAuthHandle&&) = delete;
};

std::string ReadFile(const std::filesystem::path& p) {
  std::ifstream f(p);
  REQUIRE(f.is_open());
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void WriteSettings(const std::filesystem::path& home_dir,
                   const std::string& content) {
  const auto yac_dir = home_dir / ".yac";
  std::filesystem::create_directories(yac_dir);
  std::ofstream out(yac_dir / "settings.toml");
  REQUIRE(out.is_open());
  out << content;
}

std::string ExtractQueryParam(const std::string& url, const std::string& key) {
  const std::string prefix = key + "=";
  const size_t q = url.find('?');
  if (q == std::string::npos) {
    return {};
  }
  const std::string query = url.substr(q + 1);
  size_t pos = 0;
  while (pos <= query.size()) {
    const size_t amp = query.find('&', pos);
    const size_t seg_end = (amp == std::string::npos) ? query.size() : amp;
    const std::string seg = query.substr(pos, seg_end - pos);
    if (seg.size() > prefix.size() && seg.starts_with(prefix)) {
      return seg.substr(prefix.size());
    }
    if (amp == std::string::npos) {
      break;
    }
    pos = amp + 1;
  }
  return {};
}

struct HttpResp {
  int status = 0;
  std::string body;
};

[[nodiscard]] int ConnectToPort(int port) {
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

HttpResp HttpGet(int port, const std::string& path) {
  const int fd = ConnectToPort(port);
  const std::string req = "GET " + path +
                          " HTTP/1.0\r\nHost: 127.0.0.1\r\n"
                          "Connection: close\r\n\r\n";
  ::send(fd, req.data(), req.size(), 0);

  std::string resp;
  std::array<char, 4096> buf{};
  for (;;) {
    const ssize_t n = ::recv(fd, buf.data(), buf.size(), 0);
    if (n <= 0) {
      break;
    }
    resp.append(buf.data(), static_cast<size_t>(n));
  }
  ::close(fd);

  HttpResp result;
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

ProcessHandle SpawnOAuthServer(int port, const std::string& canned_code) {
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
    std::vector<std::string> argv_storage = {MOCK_OAUTH_SERVER_PATH,
                                             "--port=" + std::to_string(port),
                                             "--canned-code=" + canned_code};
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
  return ProcessHandle{pid, out_pipe[0]};
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
      std::this_thread::sleep_for(5ms);
    }
  }
  throw std::runtime_error("mock_oauth_server did not become ready");
}

YacAuthHandle SpawnYacAuth(const std::filesystem::path& home_dir,
                           const std::string& server_id) {
  std::array<int, 2> in_pipe{};
  std::array<int, 2> out_pipe{};
  if (::pipe(in_pipe.data()) != 0 || ::pipe(out_pipe.data()) != 0) {
    throw std::runtime_error("pipe failed");
  }

  const pid_t pid = ::fork();
  if (pid < 0) {
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);
    throw std::runtime_error("fork failed");
  }

  if (pid == 0) {
    ::dup2(in_pipe[0], STDIN_FILENO);
    ::dup2(out_pipe[1], STDOUT_FILENO);
    ::close(in_pipe[0]);
    ::close(in_pipe[1]);
    ::close(out_pipe[0]);
    ::close(out_pipe[1]);

    ::setenv("HOME", home_dir.c_str(), 1);
    ::unsetenv("DBUS_SESSION_BUS_ADDRESS");

    std::vector<std::string> argv_storage = {YAC_BINARY_PATH, "mcp", "auth",
                                             server_id, "--no-browser"};
    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto& s : argv_storage) {
      argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }

  ::close(in_pipe[0]);
  ::close(out_pipe[1]);

  YacAuthHandle handle;
  handle.pid = pid;
  handle.stdin_fd = in_pipe[1];
  handle.stdout_fd = out_pipe[0];
  return handle;
}

std::string ReadAuthUrl(int fd) {
  const auto deadline = std::chrono::steady_clock::now() + 15s;
  std::string line;
  while (std::chrono::steady_clock::now() < deadline) {
    char ch = '\0';
    const ssize_t n = ::read(fd, &ch, 1);
    if (n == 1) {
      if (ch == '\n') {
        if (!line.empty() && line.back() == '\r') {
          line.pop_back();
        }
        if (line.starts_with("http://") || line.starts_with("https://")) {
          return line;
        }
        line.clear();
      } else {
        line += ch;
      }
    } else if (n == 0) {
      break;
    } else {
      std::this_thread::sleep_for(5ms);
    }
  }
  return {};
}

void WriteLineToFd(int fd, const std::string& line) {
  const std::string data = line + "\n";
  std::size_t written = 0;
  while (written < data.size()) {
    const ssize_t n = ::write(fd, data.data() + written, data.size() - written);
    if (n <= 0) {
      break;
    }
    written += static_cast<std::size_t>(n);
  }
}

int WaitForPid(pid_t pid) {
  const auto deadline = std::chrono::steady_clock::now() + 30s;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t r = ::waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      if (WIFEXITED(status) != 0) {
        return WEXITSTATUS(status);
      }
      return -1;
    }
    std::this_thread::sleep_for(50ms);
  }
  ::kill(pid, SIGKILL);
  ::waitpid(pid, &status, 0);
  return -1;
}

int RunOAuthDance(const std::filesystem::path& home_dir, int oauth_port,
                  const std::string& canned_code,
                  const std::string& server_id) {
  YacAuthHandle yac_auth = SpawnYacAuth(home_dir, server_id);

  const std::string auth_url = ReadAuthUrl(yac_auth.stdout_fd);
  if (auth_url.empty()) {
    return -1;
  }

  const std::string state = ExtractQueryParam(auth_url, "state");
  const std::string code_challenge =
      ExtractQueryParam(auth_url, "code_challenge");
  if (state.empty() || code_challenge.empty()) {
    return -1;
  }

  const std::string authorize_path =
      "/authorize?code_challenge=" + code_challenge + "&state=" + state +
      "&redirect_uri=http%3A%2F%2F127.0.0.1%3A" + std::to_string(oauth_port) +
      "%2Fcallback" + "&response_type=code&client_id=test-client";
  const HttpResp auth_resp = HttpGet(oauth_port, authorize_path);
  if (auth_resp.status != 302) {
    return -1;
  }

  const std::string callback_url =
      "http://127.0.0.1:" + std::to_string(oauth_port) +
      "/callback?code=" + canned_code + "&state=" + state;
  WriteLineToFd(yac_auth.stdin_fd, callback_url);
  ::close(yac_auth.stdin_fd);
  yac_auth.stdin_fd = -1;

  const int exit_code = WaitForPid(yac_auth.pid);
  yac_auth.pid = -1;
  return exit_code;
}

int RunE2eRunner(const std::filesystem::path& home_dir,
                 const std::string& prompt, const std::string& script_path,
                 const std::string& request_log_path) {
  const std::string runner = YAC_TEST_E2E_RUNNER_PATH;
  const std::string mock_script = "--mock-llm-script=" + script_path;
  const std::string mock_log = "--mock-request-log=" + request_log_path;
  const std::string home_str = home_dir.string();

  const pid_t pid = ::fork();
  if (pid < 0) {
    throw std::runtime_error("fork failed");
  }
  if (pid == 0) {
    ::setenv("HOME", home_str.c_str(), 1);
    std::vector<std::string> storage = {
        runner, "run", prompt, "--auto-approve", mock_script, mock_log};
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& s : storage) {
      argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }

  const auto deadline = std::chrono::steady_clock::now() + 60s;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t r = ::waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      if (WIFEXITED(status) != 0) {
        return WEXITSTATUS(status);
      }
      return -1;
    }
    std::this_thread::sleep_for(50ms);
  }
  ::kill(pid, SIGKILL);
  ::waitpid(pid, &status, 0);
  return -1;
}

std::string RunYacDebug(const std::filesystem::path& home_dir,
                        const std::string& server_id) {
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
    ::setenv("HOME", home_dir.c_str(), 1);
    std::vector<std::string> argv_storage = {YAC_BINARY_PATH, "mcp", "debug",
                                             server_id};
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

  std::string output;
  std::array<char, 4096> buf{};
  for (;;) {
    const ssize_t n = ::read(out_pipe[0], buf.data(), buf.size());
    if (n <= 0) {
      break;
    }
    output.append(buf.data(), static_cast<size_t>(n));
  }
  ::close(out_pipe[0]);

  int status = 0;
  ::waitpid(pid, &status, 0);
  return output;
}

}  // namespace

TEST_CASE("happy_path") {
  TempDir tmp;
  WriteSettings(tmp.path, ReadFile(MOCK_OAUTH_FIXTURE));

  ProcessHandle oauth_server = SpawnOAuthServer(kOAuthPort, kCannedCode);
  WaitForReady(oauth_server.read_fd);

  const int auth_exit =
      RunOAuthDance(tmp.path, kOAuthPort, kCannedCode, "mock_oauth");
  REQUIRE(auth_exit == 0);

  const auto token_path =
      tmp.path / ".yac" / "mcp" / "auth" / "mock_oauth.json";
  REQUIRE(std::filesystem::exists(token_path));
  const std::string token_json = ReadFile(token_path);
  CHECK(token_json.find("mock-access-token") != std::string::npos);

  const std::string request_log = (tmp.path / "requests.jsonl").string();
  const int runner_exit = RunE2eRunner(tmp.path, "use mcp tool",
                                       OAUTH_E2E_SCRIPT_PATH, request_log);
  REQUIRE(runner_exit == 0);

  const auto log_path = tmp.path / ".yac" / "logs" / "mcp" / "mock_oauth.log";
  CHECK(std::filesystem::exists(log_path));
}

TEST_CASE("redaction_holds") {
  TempDir tmp;
  WriteSettings(tmp.path, ReadFile(MOCK_OAUTH_FIXTURE));

  ProcessHandle oauth_server = SpawnOAuthServer(kOAuthPort, kCannedCode);
  WaitForReady(oauth_server.read_fd);

  const int auth_exit =
      RunOAuthDance(tmp.path, kOAuthPort, kCannedCode, "mock_oauth");
  REQUIRE(auth_exit == 0);

  const auto token_path =
      tmp.path / ".yac" / "mcp" / "auth" / "mock_oauth.json";
  REQUIRE(std::filesystem::exists(token_path));
  const std::string token_json = ReadFile(token_path);
  REQUIRE(token_json.find("mock-access-token") != std::string::npos);

  const auto log_dir = tmp.path / ".yac" / "logs" / "mcp";
  std::filesystem::create_directories(log_dir);
  const auto log_path = log_dir / "mock_oauth.log";
  {
    std::ofstream f(log_path, std::ios::app);
    REQUIRE(f.is_open());
    f << "2026-04-27T12:00:00Z >> Authorization: Bearer mock-access-token\n";
  }

  oauth_server.Release();
  const std::string debug_output = RunYacDebug(tmp.path, "mock_oauth");

  std::size_t redacted_count = 0;
  std::size_t pos = 0;
  while ((pos = debug_output.find("[REDACTED]", pos)) != std::string::npos) {
    ++redacted_count;
    pos += std::string_view("[REDACTED]").size();
  }

  CHECK(redacted_count > 0);
  CHECK(debug_output.find("mock-access-token") == std::string::npos);
}

}  // namespace yac::test
