#include <array>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
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
    std::string tmpl = "/tmp/yac_warm_cancel_XXXXXX";
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

std::string ReadFile(const std::filesystem::path& p) {
  std::ifstream f(p);
  if (!f.is_open()) {
    return "";
  }
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

std::string LoadFixture(const std::string& fixture_path,
                        const std::map<std::string, std::string>& subs) {
  std::ifstream f(fixture_path);
  REQUIRE(f.is_open());
  std::string content{std::istreambuf_iterator<char>(f),
                      std::istreambuf_iterator<char>()};
  for (const auto& [placeholder, value] : subs) {
    std::string::size_type pos = 0;
    while ((pos = content.find(placeholder, pos)) != std::string::npos) {
      content.replace(pos, placeholder.size(), value);
      pos += value.size();
    }
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

int RunE2eRunnerWithCancel(const std::filesystem::path& home_dir,
                           const std::string& prompt,
                           const std::string& script_path,
                           const std::string& request_log_path,
                           int cancel_after_ms, std::chrono::seconds deadline) {
  const std::string runner = YAC_TEST_E2E_RUNNER_PATH;
  const std::string mock_script = "--mock-llm-script=" + script_path;
  const std::string mock_log = "--mock-request-log=" + request_log_path;
  const std::string cancel_flag =
      "--cancel-after-ms=" + std::to_string(cancel_after_ms);
  const std::string home_str = home_dir.string();

  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error("fork failed");
  }

  if (pid == 0) {
    ::setenv("HOME", home_str.c_str(), 1);
    std::vector<std::string> storage = {runner,           "run",       prompt,
                                        "--auto-approve", mock_script, mock_log,
                                        cancel_flag};
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& s : storage) {
      argv.push_back(s.data());
    }
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }

  const auto deadline_tp = std::chrono::steady_clock::now() + deadline;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline_tp) {
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

}  // namespace

TEST_CASE("cancel_propagates") {
  TempDir tmp;
  const std::string frame_log_path = (tmp.path / "server_frames.log").string();

  const std::string settings = LoadFixture(
      SLOW_SERVER_FIXTURE,
      {{"@FAKE_MCP_SERVER_PATH@", std::string(FAKE_MCP_SERVER_PATH)},
       {"@FRAME_LOG_PATH@", frame_log_path}});
  WriteSettings(tmp.path, settings);

  const std::string request_log = (tmp.path / "requests.jsonl").string();

  const int exit_code = RunE2eRunnerWithCancel(
      tmp.path, "call a tool", WARM_CANCEL_SCRIPT_PATH, request_log, 1000, 5s);

  INFO("exit_code = " << exit_code);
  CHECK((exit_code == 0 || exit_code == 130));

  REQUIRE(std::filesystem::exists(frame_log_path));
  const std::string frames = ReadFile(frame_log_path);
  CHECK(frames.find("notifications/cancelled") != std::string::npos);
}

}  // namespace yac::test
