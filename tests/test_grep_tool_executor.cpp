#include "tool_call/grep_tool_executor.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

using namespace yac::tool_call;
using namespace yac::chat;

namespace {

ToolCallRequest MakeGrepRequest(const std::string& args_json) {
  return ToolCallRequest{
      .id = "test-grep", .name = "grep", .arguments_json = args_json};
}

class TempWorkspace {
 public:
  TempWorkspace() {
    path_ = std::filesystem::temp_directory_path() /
            ("yac_test_grep_" + std::to_string(std::hash<std::thread::id>{}(
                                    std::this_thread::get_id())));
    std::filesystem::create_directories(path_);
  }
  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

  void WriteFile(const std::string& rel_path, const std::string& content) {
    const auto full = path_ / rel_path;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream ofs(full);
    ofs << content;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

TEST_CASE("GrepTool: finds known string in fixture file") {
  TempWorkspace ws;
  ws.WriteFile("hello.txt", "hello world\nfoo bar\nbaz\n");

  WorkspaceFilesystem wfs(ws.Path());
  std::stop_source ss;

  auto result = ExecuteGrepTool(
      MakeGrepRequest(R"({"pattern":"hello world"})"), wfs, ss.get_token());

  const auto& block = std::get<GrepCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.match_count >= 1);
  REQUIRE_FALSE(block.matches.empty());
  REQUIRE(block.matches[0].content.find("hello world") != std::string::npos);
  REQUIRE(block.matches[0].line == 1);
}

TEST_CASE("GrepTool: no matches returns empty result without error") {
  TempWorkspace ws;
  ws.WriteFile("data.txt", "one\ntwo\nthree\n");

  WorkspaceFilesystem wfs(ws.Path());
  std::stop_source ss;

  auto result = ExecuteGrepTool(
      MakeGrepRequest(R"({"pattern":"ZZZNOTFOUNDZZZXXX"})"), wfs, ss.get_token());

  const auto& block = std::get<GrepCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.match_count == 0);
  REQUIRE(block.matches.empty());
  REQUIRE_FALSE(result.is_error);
}

TEST_CASE("GrepTool: rg not in PATH returns error with ripgrep message") {
  TempWorkspace ws;
  ws.WriteFile("x.txt", "content\n");

  WorkspaceFilesystem wfs(ws.Path());
  std::stop_source ss;

  const char* original_path = std::getenv("PATH");
  setenv("PATH", "/nonexistent_path_for_test_only", 1);

  auto result = ExecuteGrepTool(
      MakeGrepRequest(R"({"pattern":"content"})"), wfs, ss.get_token());

  if (original_path) {
    setenv("PATH", original_path, 1);
  } else {
    unsetenv("PATH");
  }

  const auto& block = std::get<GrepCall>(result.block);
  REQUIRE(block.is_error);
  REQUIRE(result.is_error);
  REQUIRE(block.error.find("ripgrep") != std::string::npos);
}

TEST_CASE("GrepTool: include_ignored=false skips node_modules by default") {
  TempWorkspace ws;
  ws.WriteFile(".ignore", "node_modules/\n");
  ws.WriteFile("src/main.cpp", "int main() { return 0; }\n");
  ws.WriteFile("node_modules/lib/index.js", "const x = 'hello';\n");

  WorkspaceFilesystem wfs(ws.Path());
  std::stop_source ss;

  auto result = ExecuteGrepTool(
      MakeGrepRequest(R"({"pattern":"hello"})"), wfs, ss.get_token());

  const auto& block = std::get<GrepCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  for (const auto& m : block.matches) {
    REQUIRE(m.filepath.find("node_modules") == std::string::npos);
  }
}

TEST_CASE("GrepTool: include_ignored=true finds matches in node_modules") {
  TempWorkspace ws;
  ws.WriteFile("node_modules/lib/index.js", "const greeting = 'hello';\n");

  WorkspaceFilesystem wfs(ws.Path());
  std::stop_source ss;

  auto result = ExecuteGrepTool(
      MakeGrepRequest(R"({"pattern":"greeting","include_ignored":true})"),
      wfs, ss.get_token());

  const auto& block = std::get<GrepCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.match_count >= 1);
  bool found_in_node_modules = false;
  for (const auto& m : block.matches) {
    if (m.filepath.find("node_modules") != std::string::npos) {
      found_in_node_modules = true;
    }
  }
  REQUIRE(found_in_node_modules);
}

TEST_CASE("GrepTool: include glob filters by extension") {
  TempWorkspace ws;
  ws.WriteFile("foo.cpp", "// target_token here\n");
  ws.WriteFile("foo.txt", "target_token here too\n");

  WorkspaceFilesystem wfs(ws.Path());
  std::stop_source ss;

  auto result = ExecuteGrepTool(
      MakeGrepRequest(R"({"pattern":"target_token","include":"*.cpp"})"),
      wfs, ss.get_token());

  const auto& block = std::get<GrepCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.match_count >= 1);
  for (const auto& m : block.matches) {
    REQUIRE(m.filepath.find(".txt") == std::string::npos);
  }
}

TEST_CASE("GrepTool: result_json contains expected fields") {
  TempWorkspace ws;
  ws.WriteFile("sample.txt", "unique_xyz_token_abc\n");

  WorkspaceFilesystem wfs(ws.Path());
  std::stop_source ss;

  auto result = ExecuteGrepTool(
      MakeGrepRequest(R"({"pattern":"unique_xyz_token_abc"})"),
      wfs, ss.get_token());

  REQUIRE_FALSE(result.result_json.empty());
  REQUIRE(result.result_json.find("pattern") != std::string::npos);
  REQUIRE(result.result_json.find("match_count") != std::string::npos);
  REQUIRE(result.result_json.find("matches") != std::string::npos);
  REQUIRE(result.result_json.find("truncated") != std::string::npos);
}
