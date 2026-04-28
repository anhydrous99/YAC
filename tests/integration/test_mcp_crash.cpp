#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iterator>
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
    char tmpl[] = "/tmp/yac_crash_XXXXXX";
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

int RunE2eRunnerWithStderrLog(const std::filesystem::path& home_dir,
                              const std::string& prompt,
                              const std::string& script_path,
                              const std::string& request_log_path,
                              const std::string& stderr_log_path) {
  const std::string runner = YAC_TEST_E2E_RUNNER_PATH;
  const std::string mock_script = "--mock-llm-script=" + script_path;
  const std::string mock_log = "--mock-request-log=" + request_log_path;
  const std::string home_str = home_dir.string();

  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error("fork failed");
  }

  if (pid == 0) {
    if (!stderr_log_path.empty()) {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      const int fd =
          ::open(stderr_log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (fd >= 0) {
        ::dup2(fd, STDERR_FILENO);
        ::close(fd);
      }
    }
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

  const auto deadline = std::chrono::steady_clock::now() + 90s;
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

bool StringContainsAny(const std::string& text,
                       std::initializer_list<const char*> needles) {
  for (const char* needle : needles) {
    if (text.find(needle) != std::string::npos) {
      return true;
    }
  }
  return false;
}

bool ToolPresentInLog(const std::string& log_content,
                      const std::string& tool_name) {
  return log_content.find(tool_name) != std::string::npos;
}

}  // namespace

TEST_CASE("crash_after_initialize") {
  TempDir tmp;
  WriteSettings(tmp.path, LoadFixture(CRASHING_SERVER_FIXTURE));

  const std::string request_log = (tmp.path / "requests.jsonl").string();
  const std::string stderr_log = (tmp.path / "runner_stderr.log").string();

  const int exit_code = RunE2eRunnerWithStderrLog(
      tmp.path, "hello", CRASH_SCRIPT_PATH, request_log, stderr_log);

  INFO("exit_code = " << exit_code);
  CHECK(exit_code == 0);

  if (std::filesystem::exists(request_log)) {
    const std::string log_content = ReadFile(request_log);
    CHECK_FALSE(ToolPresentInLog(log_content, "mcp_crashing_server__tool_a"));
    CHECK_FALSE(ToolPresentInLog(log_content, "mcp_crashing_server__tool_b"));
  }

  const std::string runner_stderr = ReadFile(stderr_log);
  INFO("runner stderr:\n" << runner_stderr);
  CHECK(StringContainsAny(runner_stderr,
                          {"disconnected", "Failed", "error=", "Error"}));

  const auto debug_log =
      tmp.path / ".yac" / "logs" / "mcp" / "crashing_server.log";
  CHECK(std::filesystem::exists(debug_log));
  CHECK_FALSE(ReadFile(debug_log).empty());
}

}  // namespace yac::test
