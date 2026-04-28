#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <string>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace yac::test {
namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;

struct TempDir {
  std::filesystem::path path;

  TempDir() {
    char tmpl[] = "/tmp/yac_cold_start_XXXXXX";
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
  REQUIRE(f.is_open());
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
    std::vector<char*> argv;
    std::vector<std::string> storage = {
        runner, "run", prompt, "--auto-approve", mock_script, mock_log};
    argv.reserve(storage.size() + 1);
    for (auto& s : storage) {
      argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }

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

bool ToolPresentInLog(const std::string& log_content,
                      const std::string& tool_name) {
  return log_content.find(tool_name) != std::string::npos;
}

}  // namespace

TEST_CASE("two_stdio_servers") {
  TempDir tmp;
  WriteSettings(tmp.path, LoadFixture(TWO_STDIO_SERVERS_FIXTURE));

  const std::string request_log = (tmp.path / "requests.jsonl").string();

  const int exit_code =
      RunE2eRunner(tmp.path, "list tools", COLD_START_SCRIPT_PATH, request_log);

  REQUIRE(exit_code == 0);

  const std::string log_content = ReadFile(tmp.path / "requests.jsonl");

  CHECK(ToolPresentInLog(log_content, "mcp_server_a__tool_a"));
  CHECK(ToolPresentInLog(log_content, "mcp_server_a__tool_b"));
  CHECK(ToolPresentInLog(log_content, "mcp_server_b__tool_a"));
  CHECK(ToolPresentInLog(log_content, "mcp_server_b__tool_b"));

  const auto logs_dir = tmp.path / ".yac" / "logs" / "mcp";
  CHECK(std::filesystem::exists(logs_dir / "server_a.log"));
  CHECK(std::filesystem::exists(logs_dir / "server_b.log"));

  const std::string log_a = ReadFile(logs_dir / "server_a.log");
  const std::string log_b = ReadFile(logs_dir / "server_b.log");
  CHECK_FALSE(log_a.empty());
  CHECK_FALSE(log_b.empty());
}

TEST_CASE("one_stdio_server") {
  TempDir tmp;
  WriteSettings(tmp.path, LoadFixture(ONE_STDIO_SERVER_FIXTURE));

  const std::string request_log = (tmp.path / "requests.jsonl").string();

  const int exit_code =
      RunE2eRunner(tmp.path, "list tools", COLD_START_SCRIPT_PATH, request_log);

  REQUIRE(exit_code == 0);

  const std::string log_content = ReadFile(tmp.path / "requests.jsonl");

  CHECK(ToolPresentInLog(log_content, "mcp_server_a__tool_a"));
  CHECK(ToolPresentInLog(log_content, "mcp_server_a__tool_b"));
  CHECK_FALSE(ToolPresentInLog(log_content, "mcp_server_b__tool_a"));
  CHECK_FALSE(ToolPresentInLog(log_content, "mcp_server_b__tool_b"));

  const auto logs_dir = tmp.path / ".yac" / "logs" / "mcp";
  CHECK(std::filesystem::exists(logs_dir / "server_a.log"));
  CHECK_FALSE(std::filesystem::exists(logs_dir / "server_b.log"));
}

}  // namespace yac::test
