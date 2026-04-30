#include "tool_call/edit_tool_executor.hpp"

#include <chrono>
#include <concepts>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <openai.hpp>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::tool_call;

namespace {

using Json = openai::_detail::Json;

class TempWorkspace {
 public:
  TempWorkspace() {
    std::string unique = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    unique += "_";
    unique += std::to_string(
        std::hash<std::thread::id>{}(std::this_thread::get_id()));
    root_ =
        std::filesystem::temp_directory_path() / ("yac_test_edit_" + unique);
    std::filesystem::create_directories(root_);
  }

  TempWorkspace(const TempWorkspace&) = delete;
  TempWorkspace& operator=(const TempWorkspace&) = delete;
  TempWorkspace(TempWorkspace&&) = delete;
  TempWorkspace& operator=(TempWorkspace&&) = delete;

  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(root_, ec);
  }

  [[nodiscard]] const std::filesystem::path& Root() const { return root_; }

  void Write(const std::string& relative_path,
             const std::string& content) const {
    const auto path = root_ / relative_path;
    std::filesystem::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    REQUIRE(file.good());
    file.write(content.data(), static_cast<std::streamsize>(content.size()));
    REQUIRE(file.good());
  }

  [[nodiscard]] std::string Read(const std::string& relative_path) const {
    std::ifstream file(root_ / relative_path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file),
            std::istreambuf_iterator<char>()};
  }

 private:
  std::filesystem::path root_;
};

ToolCallRequest MakeEditRequest(const Json& args) {
  return ToolCallRequest{.id = "test-edit-1",
                         .name = std::string(kFileEditToolName),
                         .arguments_json = args.dump()};
}

void RequireRuntimeErrorContains(const std::invocable auto& fn,
                                 const std::string& expected_substring) {
  try {
    fn();
    FAIL("Expected std::runtime_error");
  } catch (const std::runtime_error& error) {
    REQUIRE(std::string(error.what()).find(expected_substring) !=
            std::string::npos);
  }
}

}  // namespace

TEST_CASE("EditTool: simple replacement updates file and reports diff") {
  TempWorkspace workspace;
  workspace.Write("src/demo.txt", "alpha\nbeta\ngamma\n");

  WorkspaceFilesystem filesystem(workspace.Root());
  const auto result =
      ExecuteEditTool(MakeEditRequest({{"filepath", "src/demo.txt"},
                                       {"old_string", "beta"},
                                       {"new_string", "delta"}}),
                      filesystem);

  REQUIRE_FALSE(result.is_error);
  REQUIRE(workspace.Read("src/demo.txt") == "alpha\ndelta\ngamma\n");

  const auto& call = std::get<FileEditCall>(result.block);
  REQUIRE(call.filepath == "src/demo.txt");
  REQUIRE_FALSE(call.diff.empty());

  const auto json = Json::parse(result.result_json);
  REQUIRE(json.at("filepath") == "src/demo.txt");
  REQUIRE(json.at("diff_lines") == call.diff.size());
  REQUIRE(json.at("additions") == 1);
  REQUIRE(json.at("deletions") == 1);
}

TEST_CASE("EditTool: empty old_string is rejected") {
  TempWorkspace workspace;
  workspace.Write("file.txt", "hello\n");
  WorkspaceFilesystem filesystem(workspace.Root());

  RequireRuntimeErrorContains(
      [&] {
        (void)ExecuteEditTool(MakeEditRequest({{"filepath", "file.txt"},
                                               {"old_string", ""},
                                               {"new_string", "hello"}}),
                              filesystem);
      },
      "must not be empty");
}

TEST_CASE("EditTool: identical old and new strings are rejected") {
  TempWorkspace workspace;
  workspace.Write("file.txt", "hello\n");
  WorkspaceFilesystem filesystem(workspace.Root());

  RequireRuntimeErrorContains(
      [&] {
        (void)ExecuteEditTool(MakeEditRequest({{"filepath", "file.txt"},
                                               {"old_string", "hello"},
                                               {"new_string", "hello"}}),
                              filesystem);
      },
      "identical");
}

TEST_CASE("EditTool: missing file is rejected") {
  TempWorkspace workspace;
  WorkspaceFilesystem filesystem(workspace.Root());

  RequireRuntimeErrorContains(
      [&] {
        (void)ExecuteEditTool(MakeEditRequest({{"filepath", "missing.txt"},
                                               {"old_string", "a"},
                                               {"new_string", "b"}}),
                              filesystem);
      },
      "file not found: missing.txt");
}

TEST_CASE("EditTool: binary files are rejected") {
  TempWorkspace workspace;
  workspace.Write("file.bin", std::string{"abc\0def", 7});
  WorkspaceFilesystem filesystem(workspace.Root());

  RequireRuntimeErrorContains(
      [&] {
        (void)ExecuteEditTool(MakeEditRequest({{"filepath", "file.bin"},
                                               {"old_string", "abc"},
                                               {"new_string", "xyz"}}),
                              filesystem);
      },
      "Cannot edit binary file: file.bin");
}

TEST_CASE(
    "EditTool: no match without replace_all reports exact-match failure") {
  TempWorkspace workspace;
  workspace.Write("file.txt", "alpha\nbeta\n");
  WorkspaceFilesystem filesystem(workspace.Root());

  RequireRuntimeErrorContains(
      [&] {
        (void)ExecuteEditTool(MakeEditRequest({{"filepath", "file.txt"},
                                               {"old_string", "gamma"},
                                               {"new_string", "delta"}}),
                              filesystem);
      },
      "Could not find old_string in file");
}

TEST_CASE("EditTool: multiple matches without replace_all throws") {
  TempWorkspace workspace;
  workspace.Write("file.txt", "target\nmid\ntarget\n");
  WorkspaceFilesystem filesystem(workspace.Root());

  RequireRuntimeErrorContains(
      [&] {
        (void)ExecuteEditTool(MakeEditRequest({{"filepath", "file.txt"},
                                               {"old_string", "target"},
                                               {"new_string", "updated"}}),
                              filesystem);
      },
      "Found multiple matches");
}

TEST_CASE("EditTool: replace_all updates every match") {
  TempWorkspace workspace;
  workspace.Write("file.txt", "foo\nbar\nfoo\nfoo\n");
  WorkspaceFilesystem filesystem(workspace.Root());

  const auto result = ExecuteEditTool(MakeEditRequest({{"filepath", "file.txt"},
                                                       {"old_string", "foo"},
                                                       {"new_string", "qux"},
                                                       {"replace_all", true}}),
                                      filesystem);

  REQUIRE(workspace.Read("file.txt") == "qux\nbar\nqux\nqux\n");
  const auto json = Json::parse(result.result_json);
  REQUIRE(json.at("additions") == 3);
  REQUIRE(json.at("deletions") == 3);
}

TEST_CASE("EditTool: line-trimmed fallback tolerates whitespace drift") {
  TempWorkspace workspace;
  workspace.Write("file.txt", "first\nvalue = 1   \nnext();\nlast\n");
  WorkspaceFilesystem filesystem(workspace.Root());

  const auto result =
      ExecuteEditTool(MakeEditRequest({{"filepath", "file.txt"},
                                       {"old_string", "value = 1\nnext();\n"},
                                       {"new_string", "value = 2\nnext();\n"}}),
                      filesystem);

  REQUIRE(workspace.Read("file.txt") == "first\nvalue = 2\nnext();\nlast\n");
  REQUIRE_FALSE(std::get<FileEditCall>(result.block).diff.empty());
}

TEST_CASE(
    "EditTool: whitespace-normalized fallback tolerates indentation drift") {
  TempWorkspace workspace;
  workspace.Write("code.cpp", "if (ready) {\n    call(a,\tb);\n}\n");
  WorkspaceFilesystem filesystem(workspace.Root());

  const auto result = ExecuteEditTool(
      MakeEditRequest({{"filepath", "code.cpp"},
                       {"old_string", "if (ready) {\n  call(a, b);\n}\n"},
                       {"new_string", "if (ready) {\n  call(updated);\n}\n"}}),
      filesystem);

  REQUIRE(workspace.Read("code.cpp") == "if (ready) {\n  call(updated);\n}\n");
  REQUIRE_FALSE(std::get<FileEditCall>(result.block).diff.empty());
}
