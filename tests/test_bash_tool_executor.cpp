#include "tool_call/bash_tool_executor.hpp"
#include "tool_call/executor.hpp"

#include <filesystem>
#include <stop_token>
#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace yac::tool_call;
using namespace yac::chat;

namespace {

ToolCallRequest MakeBashRequest(const std::string& command,
                                int timeout_ms = 0) {
  std::string args = R"({"command":")" + command + R"(")";
  if (timeout_ms > 0) {
    args += R"(,"timeout_ms":)" + std::to_string(timeout_ms);
  }
  args += "}";
  return ToolCallRequest{
      .id = "test-1", .name = "bash", .arguments_json = args};
}

class TempWorkspace {
 public:
  TempWorkspace() {
    path_ = std::filesystem::temp_directory_path() /
            ("yac_test_bash_" + std::to_string(std::hash<std::thread::id>{}(
                                    std::this_thread::get_id())));
    std::filesystem::create_directories(path_);
  }
  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

}  // namespace

TEST_CASE("BashTool: echo captures stdout") {
  TempWorkspace ws;
  std::stop_source ss;
  auto result =
      ExecuteBashTool(MakeBashRequest("echo hello"), ws.Path(), ss.get_token());
  const auto& block = std::get<BashCall>(result.block);
  REQUIRE(block.exit_code == 0);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.output.find("hello") != std::string::npos);
}

TEST_CASE("BashTool: non-zero exit code marks error") {
  TempWorkspace ws;
  std::stop_source ss;
  auto result =
      ExecuteBashTool(MakeBashRequest("exit 42"), ws.Path(), ss.get_token());
  const auto& block = std::get<BashCall>(result.block);
  REQUIRE(block.exit_code == 42);
  REQUIRE(block.is_error);
  REQUIRE(result.is_error);
}

TEST_CASE("BashTool: stderr merged into output") {
  TempWorkspace ws;
  std::stop_source ss;
  auto result = ExecuteBashTool(MakeBashRequest("echo out; echo err >&2"),
                                ws.Path(), ss.get_token());
  const auto& block = std::get<BashCall>(result.block);
  REQUIRE(block.output.find("out") != std::string::npos);
  REQUIRE(block.output.find("err") != std::string::npos);
}

TEST_CASE("BashTool: timeout kills runaway command") {
  TempWorkspace ws;
  std::stop_source ss;
  const auto start = std::chrono::steady_clock::now();
  auto result = ExecuteBashTool(MakeBashRequest("sleep 60", 300), ws.Path(),
                                ss.get_token());
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  REQUIRE(result.is_error);
  REQUIRE(elapsed < 5000);
  const auto& block = std::get<BashCall>(result.block);
  REQUIRE(block.is_error);
}

TEST_CASE("BashTool: output truncated at 16KB") {
  TempWorkspace ws;
  std::stop_source ss;
  auto result = ExecuteBashTool(MakeBashRequest("seq 1 100000"), ws.Path(),
                                ss.get_token());
  const auto& block = std::get<BashCall>(result.block);
  REQUIRE(block.output.size() <= 16384 + 100);
  REQUIRE(block.output.find("[output truncated") != std::string::npos);
}

TEST_CASE("BashTool: workspace cwd respected") {
  TempWorkspace ws;
  std::stop_source ss;
  auto result =
      ExecuteBashTool(MakeBashRequest("pwd"), ws.Path(), ss.get_token());
  const auto& block = std::get<BashCall>(result.block);
  REQUIRE(block.exit_code == 0);
  REQUIRE(block.output.find(ws.Path().string()) != std::string::npos);
}
