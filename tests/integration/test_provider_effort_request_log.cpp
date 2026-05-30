#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <stdexcept>
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
    std::string tmpl = "/tmp/yac_effort_request_log_XXXXXX";
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

void WriteSettings(const std::filesystem::path& home_dir,
                   const std::string& content) {
  const auto yac_dir = home_dir / ".yac";
  std::filesystem::create_directories(yac_dir);
  std::ofstream out(yac_dir / "settings.toml");
  REQUIRE(out.is_open());
  out << content;
}

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  REQUIRE(input.is_open());
  return {std::istreambuf_iterator<char>(input),
          std::istreambuf_iterator<char>()};
}

int RunE2eRunner(const std::filesystem::path& home_dir,
                 const std::string& prompt, const std::string& script_path,
                 const std::string& request_log_path,
                 const std::string& provider_id) {
  const std::string runner = YAC_TEST_E2E_RUNNER_PATH;
  const std::string mock_script = "--mock-llm-script=" + script_path;
  const std::string mock_log = "--mock-request-log=" + request_log_path;
  const std::string mock_provider = "--mock-provider-id=" + provider_id;
  const std::string home_str = home_dir.string();

  const pid_t pid = fork();
  if (pid < 0) {
    throw std::runtime_error("fork failed");
  }

  if (pid == 0) {
    ::setenv("HOME", home_str.c_str(), 1);
    std::vector<std::string> storage = {runner,           "run",       prompt,
                                        "--auto-approve", mock_script, mock_log,
                                        mock_provider};
    std::vector<char*> argv;
    argv.reserve(storage.size() + 1);
    for (auto& item : storage) {
      argv.push_back(item.data());
    }
    argv.push_back(nullptr);
    ::execv(argv[0], argv.data());
    ::_exit(127);
  }

  const auto deadline = std::chrono::steady_clock::now() + 30s;
  int status = 0;
  while (std::chrono::steady_clock::now() < deadline) {
    const pid_t result = ::waitpid(pid, &status, WNOHANG);
    if (result == pid) {
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

TEST_CASE("request log records persisted OpenAI chat completions effort") {
  TempDir tmp;
  WriteSettings(tmp.path,
                "[provider]\n"
                "id = \"openai\"\n"
                "model = \"gpt-5.5\"\n"
                "options.reasoning = \"{effort='low'}\"\n"
                "[[provider.model_settings]]\n"
                "provider = \"openai\"\n"
                "model = \"gpt-5.5\"\n"
                "effort = \"high\"\n");

  const auto request_log = tmp.path / "requests.jsonl";
  const int exit_code = RunE2eRunner(tmp.path, "hello", SAMPLE_SCRIPT_PATH,
                                     request_log.string(), "openai");

  REQUIRE(exit_code == 0);

  const std::string log_content = ReadFile(request_log);
  const Json request_body =
      Json::parse(log_content.substr(0, log_content.find('\n')));
  REQUIRE(request_body["provider_id"].get<std::string>() == "openai");
  REQUIRE(request_body["model"].get<std::string>() == "gpt-5.5");
  REQUIRE(request_body["reasoning_effort"].get<std::string>() == "high");
  REQUIRE(request_body.contains("chat_completions_payload"));
  const Json& payload = request_body["chat_completions_payload"];
  REQUIRE(payload["model"].get<std::string>() == "gpt-5.5");
  REQUIRE(payload["stream"].get<bool>());
  REQUIRE(payload["reasoning_effort"].get<std::string>() == "high");
  REQUIRE_FALSE(payload.contains("reasoning"));
}

}  // namespace yac::test
