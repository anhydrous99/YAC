#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace yac::test {
namespace {

using namespace std::chrono_literals;

struct TempDir {
  std::filesystem::path path;

  TempDir() {
    char tmpl[] = "/tmp/yac_shutdown_XXXXXX";
    const char* result = ::mkdtemp(tmpl);
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

std::string ReadFile(const std::filesystem::path& p) {
  std::ifstream f(p);
  if (!f.is_open()) {
    return "";
  }
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::string LoadFixture(const std::string& fixture_path) {
  std::ifstream f(fixture_path);
  REQUIRE(f.is_open());
  std::string content{std::istreambuf_iterator<char>(f),
                      std::istreambuf_iterator<char>()};
  const std::string placeholder = "@FAKE_MCP_SERVER_PATH@";
  const std::string binary = FAKE_MCP_SERVER_PATH;
  std::string::size_type pos = 0;
  while ((pos = content.find(placeholder, pos)) != std::string::npos) {
    content.replace(pos, placeholder.size(), binary);
    pos += binary.size();
  }
  return content;
}

void WriteSettings(const std::filesystem::path& home_dir,
                   const std::string& content) {
  const auto yac_dir = home_dir / ".yac";
  std::filesystem::create_directories(yac_dir);
  std::ofstream out(yac_dir / "settings.toml");
  REQUIRE(out.is_open());
  out << content;
}

int RunE2eRunner(const std::filesystem::path& home_dir,
                 const std::string& prompt, const std::string& script_path,
                 const std::string& request_log_path) {
  const std::string runner = YAC_TEST_E2E_RUNNER_PATH;
  const std::string mock_script = "--mock-llm-script=" + script_path;
  const std::string mock_log = "--mock-request-log=" + request_log_path;
  const std::string home_str = home_dir.string();

  const pid_t pid = fork();
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

  const auto deadline = std::chrono::steady_clock::now() + 120s;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t r = ::waitpid(pid, &status, WNOHANG);
    if (r == pid) {
      if (WIFEXITED(status) != 0) {
        return WEXITSTATUS(status);
      }
      if (WIFSIGNALED(status) != 0) {
        return 128 + WTERMSIG(status);
      }
      return -1;
    }
    std::this_thread::sleep_for(50ms);
  }
  ::kill(pid, SIGKILL);
  ::waitpid(pid, &status, 0);
  return -1;
}

std::set<pid_t> GetFakeMcpServerPids() {
  std::set<pid_t> pids;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  FILE* pipe = ::popen("pgrep -x fake_mcp_server 2>/dev/null", "r");
  if (pipe == nullptr) {
    return pids;
  }
  int pid_val = 0;
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  while (std::fscanf(pipe, "%d", &pid_val) == 1) {
    pids.insert(static_cast<pid_t>(pid_val));
  }
  ::pclose(pipe);
  return pids;
}

bool FileContains(const std::filesystem::path& p, const std::string& needle) {
  const std::string content = ReadFile(p);
  return content.find(needle) != std::string::npos;
}

}  // namespace

TEST_CASE("graceful") {
  TempDir tmp;
  WriteSettings(tmp.path, LoadFixture(TWO_STDIO_SERVERS_FIXTURE));

  const std::string request_log = (tmp.path / "requests.jsonl").string();

  const auto before_pids = GetFakeMcpServerPids();

  const int exit_code =
      RunE2eRunner(tmp.path, "hello", SHUTDOWN_SCRIPT_PATH, request_log);

  REQUIRE(exit_code == 0);

  // Allow processes to fully exit after shutdown signal
  std::this_thread::sleep_for(100ms);

  const auto deadline = std::chrono::steady_clock::now() + 5s;
  bool no_stragglers = false;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto after_pids = GetFakeMcpServerPids();
    bool found_new = false;
    for (const auto& p : after_pids) {
      if (!before_pids.contains(p)) {
        found_new = true;
        break;
      }
    }
    if (!found_new) {
      no_stragglers = true;
      break;
    }
    std::this_thread::sleep_for(50ms);
  }
  CHECK(no_stragglers);

  const auto logs_dir = tmp.path / ".yac" / "logs" / "mcp";
  CHECK(std::filesystem::exists(logs_dir / "server_a.log"));
  CHECK(std::filesystem::exists(logs_dir / "server_b.log"));
  CHECK(FileContains(logs_dir / "server_a.log", "connection_closed"));
  CHECK(FileContains(logs_dir / "server_b.log", "connection_closed"));
}

}  // namespace yac::test
