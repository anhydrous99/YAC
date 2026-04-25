#include "chat/types.hpp"
#include "core_types/tool_call_types.hpp"
#include "tool_call/glob_tool_executor.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <openai.hpp>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::chat::ToolCallRequest;
using yac::tool_call::ExecuteGlobTool;
using yac::tool_call::WorkspaceFilesystem;

namespace {

using Json = openai::_detail::Json;

class TempWorkspace {
 public:
  TempWorkspace() {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("yac_test_glob_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }
  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }
  TempWorkspace(const TempWorkspace&) = delete;
  TempWorkspace& operator=(const TempWorkspace&) = delete;
  TempWorkspace(TempWorkspace&&) = delete;
  TempWorkspace& operator=(TempWorkspace&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

  void CreateFile(const std::string& relative_path,
                  const std::string& content = "") const {
    const auto full = path_ / relative_path;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream f(full);
    f << content;
  }

  void WriteGitignore(const std::string& content) const {
    CreateFile(".gitignore", content);
  }

 private:
  std::filesystem::path path_;
};

ToolCallRequest MakeRequest(Json args) {
  return ToolCallRequest{.name = "glob", .arguments_json = args.dump()};
}

}  // namespace

TEST_CASE("glob happy path: **/*.hpp returns matching header files") {
  TempWorkspace ws;
  ws.CreateFile("src/foo.hpp");
  ws.CreateFile("src/bar.hpp");
  ws.CreateFile("src/main.cpp");
  ws.CreateFile("include/baz.hpp");

  WorkspaceFilesystem wfs(ws.Path());
  const auto result =
      ExecuteGlobTool(MakeRequest({{"pattern", "**/*.hpp"}}), wfs);

  const auto& block = std::get<yac::tool_call::GlobCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.matched_files.size() == 3);

  const auto j = Json::parse(result.result_json);
  REQUIRE(j["match_count"] == 3);
  REQUIRE_FALSE(j["truncated"].get<bool>());
}

TEST_CASE("glob specific pattern: exact filename without wildcards") {
  TempWorkspace ws;
  ws.CreateFile("executor.hpp");
  ws.CreateFile("executor.cpp");
  ws.CreateFile("types.hpp");

  WorkspaceFilesystem wfs(ws.Path());
  const auto result =
      ExecuteGlobTool(MakeRequest({{"pattern", "executor.hpp"}}), wfs);

  const auto& block = std::get<yac::tool_call::GlobCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.matched_files.size() == 1);
}

TEST_CASE("glob no matches: unrecognized extension returns empty result") {
  TempWorkspace ws;
  ws.CreateFile("src/main.cpp");
  ws.CreateFile("include/types.hpp");

  WorkspaceFilesystem wfs(ws.Path());
  const auto result =
      ExecuteGlobTool(MakeRequest({{"pattern", "**/*.nonexistent"}}), wfs);

  const auto& block = std::get<yac::tool_call::GlobCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.matched_files.empty());

  const auto j = Json::parse(result.result_json);
  REQUIRE(j["match_count"] == 0);
  REQUIRE_FALSE(j["truncated"].get<bool>());
}

TEST_CASE("glob gitignore filtering: node_modules files excluded by default") {
  TempWorkspace ws;
  ws.WriteGitignore("node_modules/\n");
  ws.CreateFile("node_modules/index.js");
  ws.CreateFile("node_modules/lib/util.js");
  ws.CreateFile("src/app.js");

  WorkspaceFilesystem wfs(ws.Path());
  const auto result =
      ExecuteGlobTool(MakeRequest({{"pattern", "**/*.js"}}), wfs);

  const auto& block = std::get<yac::tool_call::GlobCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.matched_files.size() == 1);
  REQUIRE(block.matched_files[0].find("app.js") != std::string::npos);
}

TEST_CASE("glob include_ignored=true: ignored files are included") {
  TempWorkspace ws;
  ws.WriteGitignore("node_modules/\n");
  ws.CreateFile("node_modules/index.js");
  ws.CreateFile("node_modules/lib/util.js");
  ws.CreateFile("src/app.js");

  WorkspaceFilesystem wfs(ws.Path());
  const auto result = ExecuteGlobTool(
      MakeRequest({{"pattern", "**/*.js"}, {"include_ignored", true}}), wfs);

  const auto& block = std::get<yac::tool_call::GlobCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.matched_files.size() == 3);
}

TEST_CASE(
    "glob 200-result cap: truncates at kMaxResults and sets truncated flag") {
  TempWorkspace ws;
  ws.WriteGitignore(".git/\n");
  for (int i = 0; i < 250; ++i) {
    ws.CreateFile("files/file_" + std::to_string(i) + ".txt");
  }

  WorkspaceFilesystem wfs(ws.Path());
  const auto result =
      ExecuteGlobTool(MakeRequest({{"pattern", "**/*.txt"}}), wfs);

  const auto& block = std::get<yac::tool_call::GlobCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.matched_files.size() == 200);

  const auto j = Json::parse(result.result_json);
  REQUIRE(j["match_count"] == 200);
  REQUIRE(j["truncated"].get<bool>());
}

TEST_CASE("glob path arg: restricts walk to specified subdirectory") {
  TempWorkspace ws;
  ws.CreateFile("src/foo.hpp");
  ws.CreateFile("src/bar.hpp");
  ws.CreateFile("include/baz.hpp");

  WorkspaceFilesystem wfs(ws.Path());
  const auto result = ExecuteGlobTool(
      MakeRequest({{"pattern", "**/*.hpp"}, {"path", "src"}}), wfs);

  const auto& block = std::get<yac::tool_call::GlobCall>(result.block);
  REQUIRE_FALSE(block.is_error);
  REQUIRE(block.matched_files.size() == 2);
  for (const auto& f : block.matched_files) {
    REQUIRE(f.find("src") != std::string::npos);
  }
}

TEST_CASE("glob result json contains correct pattern field") {
  TempWorkspace ws;
  ws.CreateFile("src/main.cpp");

  WorkspaceFilesystem wfs(ws.Path());
  const auto result =
      ExecuteGlobTool(MakeRequest({{"pattern", "**/*.cpp"}}), wfs);

  const auto j = Json::parse(result.result_json);
  REQUIRE(j["pattern"] == "**/*.cpp");
  REQUIRE(j.contains("match_count"));
  REQUIRE(j.contains("matches"));
  REQUIRE(j.contains("truncated"));
}
