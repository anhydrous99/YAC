#include <array>
#include <chrono>
#include <csignal>
#include <cstring>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace yac::test {
namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;

struct ProcessHandle {
  pid_t pid = -1;
  int write_fd = -1;
  int read_fd = -1;
};

[[nodiscard]] ProcessHandle SpawnFakeServer(
    const std::vector<std::string>& extra_args) {
  std::array<int, 2> stdin_pipe{};
  std::array<int, 2> stdout_pipe{};
  if (pipe(stdin_pipe.data()) != 0) {
    throw std::runtime_error("pipe(stdin) failed");
  }
  if (pipe(stdout_pipe.data()) != 0) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    throw std::runtime_error("pipe(stdout) failed");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);
    throw std::runtime_error("fork failed");
  }

  if (pid == 0) {
    dup2(stdin_pipe[0], STDIN_FILENO);
    dup2(stdout_pipe[1], STDOUT_FILENO);
    close(stdin_pipe[0]);
    close(stdin_pipe[1]);
    close(stdout_pipe[0]);
    close(stdout_pipe[1]);

    std::vector<std::string> argv_storage;
    argv_storage.emplace_back(FAKE_MCP_SERVER_PATH);
    for (const auto& arg : extra_args) {
      argv_storage.push_back(arg);
    }
    std::vector<char*> argv;
    argv.reserve(argv_storage.size() + 1);
    for (auto& item : argv_storage) {
      argv.push_back(item.data());
    }
    argv.push_back(nullptr);
    execv(argv[0], argv.data());
    _exit(127);
  }

  close(stdin_pipe[0]);
  close(stdout_pipe[1]);
  return ProcessHandle{
      .pid = pid,
      .write_fd = stdin_pipe[1],
      .read_fd = stdout_pipe[0],
  };
}

void WriteFrame(int fd, const Json& message) {
  const std::string payload = message.dump() + '\n';
  std::size_t written = 0;
  while (written < payload.size()) {
    const ssize_t n =
        ::write(fd, payload.data() + written, payload.size() - written);
    if (n <= 0) {
      throw std::runtime_error("write failed");
    }
    written += static_cast<std::size_t>(n);
  }
}

[[nodiscard]] std::string ReadLine(int fd, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::string line;
  while (std::chrono::steady_clock::now() < deadline) {
    char ch = 0;
    const ssize_t n = ::read(fd, &ch, 1);
    if (n == 1) {
      if (ch == '\n') {
        return line;
      }
      line.push_back(ch);
      continue;
    }
    if (n == 0) {
      return line;
    }
    // SLEEP-RATIONALE: polls non-blocking pipe read without busy-waiting
    std::this_thread::sleep_for(5ms);
  }
  return line;
}

[[nodiscard]] int WaitForExit(pid_t pid, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t result = waitpid(pid, &status, WNOHANG);
    if (result == pid) {
      if (WIFEXITED(status) != 0) {
        return WEXITSTATUS(status);
      }
      return -1;
    }
    // SLEEP-RATIONALE: polls waitpid(WNOHANG) without busy-waiting
    std::this_thread::sleep_for(10ms);
  }
  kill(pid, SIGKILL);
  waitpid(pid, &status, 0);
  return -1;
}

void Cleanup(ProcessHandle& handle) {
  if (handle.write_fd >= 0) {
    close(handle.write_fd);
    handle.write_fd = -1;
  }
  if (handle.read_fd >= 0) {
    close(handle.read_fd);
    handle.read_fd = -1;
  }
  if (handle.pid > 0) {
    int status = 0;
    if (waitpid(handle.pid, &status, WNOHANG) == 0) {
      kill(handle.pid, SIGKILL);
      waitpid(handle.pid, &status, 0);
    }
    handle.pid = -1;
  }
}

[[nodiscard]] Json MakeInitializeRequest(int id) {
  return Json{
      {"jsonrpc", "2.0"},
      {"id", id},
      {"method", "initialize"},
      {"params",
       Json{{"protocolVersion", "2025-11-25"},
            {"capabilities", Json::object()},
            {"clientInfo", Json{{"name", "smoke"}, {"version", "0.1"}}}}},
  };
}

}  // namespace

TEST_CASE("responds_to_initialize") {
  ProcessHandle handle = SpawnFakeServer({});
  WriteFrame(handle.write_fd, MakeInitializeRequest(1));

  const std::string line = ReadLine(handle.read_fd, 2s);
  REQUIRE_FALSE(line.empty());

  const Json response = Json::parse(line);
  REQUIRE(response["jsonrpc"] == "2.0");
  REQUIRE(response["id"] == 1);
  REQUIRE(response.contains("result"));
  REQUIRE(response["result"]["protocolVersion"] == "2025-11-25");
  REQUIRE(response["result"].contains("serverInfo"));
  REQUIRE(response["result"]["serverInfo"]["name"] == "fake_mcp_server");
  REQUIRE(response["result"].contains("capabilities"));

  Cleanup(handle);
}

TEST_CASE("crash_after_initialize") {
  ProcessHandle handle = SpawnFakeServer({"--crash-after=initialize"});
  WriteFrame(handle.write_fd, MakeInitializeRequest(1));

  const std::string line = ReadLine(handle.read_fd, 2s);
  REQUIRE_FALSE(line.empty());

  close(handle.write_fd);
  handle.write_fd = -1;
  close(handle.read_fd);
  handle.read_fd = -1;

  const int exit_code = WaitForExit(handle.pid, 2s);
  handle.pid = -1;
  REQUIRE(exit_code != 0);
  REQUIRE(exit_code != -1);
}

}  // namespace yac::test
