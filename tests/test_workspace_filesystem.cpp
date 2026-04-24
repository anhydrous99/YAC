#include "tool_call/workspace_filesystem.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using yac::tool_call::WorkspaceFilesystem;

namespace {

// Each test case gets a unique temp dir so parallel ctest runs do not
// clobber each other. Cleaned up on destruction.
class TempWorkspace {
 public:
  TempWorkspace() {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("yac_test_workspace_" + std::to_string(counter.fetch_add(1)));
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

 private:
  std::filesystem::path path_;
};

}  // namespace

TEST_CASE("ResolvePath rejects absolute paths outside the workspace") {
  TempWorkspace workspace;
  WorkspaceFilesystem fs(workspace.Path());
  REQUIRE_THROWS_AS(fs.ResolvePath("/etc/passwd"), std::runtime_error);
}

TEST_CASE("ResolvePath rejects ../ traversal escapes") {
  TempWorkspace workspace;
  WorkspaceFilesystem fs(workspace.Path());
  REQUIRE_THROWS_AS(fs.ResolvePath("../outside.txt"), std::runtime_error);
}

TEST_CASE("ResolvePath accepts regular files inside the workspace") {
  TempWorkspace workspace;
  std::filesystem::create_directories(workspace.Path() / "sub");
  WorkspaceFilesystem fs(workspace.Path());
  const auto resolved = fs.ResolvePath("sub/note.txt");
  REQUIRE(resolved.string().starts_with(workspace.Path().string()));
}

TEST_CASE("ResolvePath rejects a symlink that points outside the workspace") {
  TempWorkspace workspace;
  std::error_code ec;
  std::filesystem::create_symlink("/etc/hostname", workspace.Path() / "escape",
                                  ec);
  REQUIRE_FALSE(ec);
  WorkspaceFilesystem fs(workspace.Path());
  REQUIRE_THROWS_AS(fs.ResolvePath("escape"), std::runtime_error);
}

TEST_CASE("ResolvePath rejects paths whose parent is an escaping symlink") {
  TempWorkspace workspace;
  std::error_code ec;
  std::filesystem::create_directory_symlink("/etc",
                                            workspace.Path() / "etc_link", ec);
  REQUIRE_FALSE(ec);
  WorkspaceFilesystem fs(workspace.Path());
  REQUIRE_THROWS_AS(fs.ResolvePath("etc_link/hostname"), std::runtime_error);
}

TEST_CASE("ReadFile refuses to read through a symlink, even inside workspace") {
  TempWorkspace workspace;
  {
    std::ofstream real_file(workspace.Path() / "secret.txt");
    real_file << "secret";
  }
  std::error_code ec;
  std::filesystem::create_symlink(workspace.Path() / "secret.txt",
                                  workspace.Path() / "alias.txt", ec);
  REQUIRE_FALSE(ec);
  REQUIRE_THROWS_AS(
      WorkspaceFilesystem::ReadFile(workspace.Path() / "alias.txt"),
      std::runtime_error);
}

TEST_CASE("WriteFile refuses to write through a symlink") {
  TempWorkspace workspace;
  {
    std::ofstream real_file(workspace.Path() / "target.txt");
    real_file << "before";
  }
  std::error_code ec;
  std::filesystem::create_symlink(workspace.Path() / "target.txt",
                                  workspace.Path() / "alias.txt", ec);
  REQUIRE_FALSE(ec);
  REQUIRE_THROWS_AS(
      WorkspaceFilesystem::WriteFile(workspace.Path() / "alias.txt", "after"),
      std::runtime_error);

  std::ifstream check(workspace.Path() / "target.txt");
  std::string content((std::istreambuf_iterator<char>(check)),
                      std::istreambuf_iterator<char>());
  REQUIRE(content == "before");
}

TEST_CASE("ReadFile and WriteFile still work on regular files") {
  TempWorkspace workspace;
  const auto path = workspace.Path() / "sub" / "note.txt";
  WorkspaceFilesystem::WriteFile(path, "hello");
  REQUIRE(WorkspaceFilesystem::ReadFile(path) == "hello");
}
