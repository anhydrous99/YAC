#include "tool_call/gitignore_filter.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using yac::tool_call::GitignoreFilter;

namespace {

class TempWorkspace {
 public:
  TempWorkspace() {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("yac_test_gitignore_" + std::to_string(counter.fetch_add(1)));
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

  void WriteGitignore(const std::string& content) const {
    std::ofstream f(path_ / ".gitignore");
    f << content;
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

TEST_CASE("directory pattern skips contents but not unrelated paths") {
  TempWorkspace ws;
  ws.WriteGitignore("node_modules/\n");
  GitignoreFilter filter(ws.Path());

  REQUIRE(filter.ShouldSkip("node_modules/foo.js"));
  REQUIRE(filter.ShouldSkip("node_modules/sub/bar.ts"));
  REQUIRE_FALSE(filter.ShouldSkip("src/main.cpp"));
  REQUIRE_FALSE(filter.ShouldSkip("README.md"));
}

TEST_CASE("negation pattern un-skips specific file") {
  TempWorkspace ws;
  ws.WriteGitignore("*.log\n!important.log\n");
  GitignoreFilter filter(ws.Path());

  REQUIRE(filter.ShouldSkip("foo.log"));
  REQUIRE(filter.ShouldSkip("debug.log"));
  REQUIRE_FALSE(filter.ShouldSkip("important.log"));
  REQUIRE_FALSE(filter.ShouldSkip("src/main.cpp"));
}

TEST_CASE("root-anchored pattern skips at root but not in subdirectories") {
  TempWorkspace ws;
  ws.WriteGitignore("/build\n");
  GitignoreFilter filter(ws.Path());

  REQUIRE(filter.ShouldSkip("build/foo"));
  REQUIRE(filter.ShouldSkip("build/sub/artifact.o"));
  REQUIRE_FALSE(filter.ShouldSkip("src/build/foo"));
  REQUIRE_FALSE(filter.ShouldSkip("src/main.cpp"));
}

TEST_CASE("fallback deny-list used when no .gitignore present") {
  TempWorkspace ws;
  GitignoreFilter filter(ws.Path());

  REQUIRE(filter.ShouldSkip("node_modules/foo.js"));
  REQUIRE(filter.ShouldSkip("build/output.o"));
  REQUIRE(filter.ShouldSkip(".git/config"));
  REQUIRE(filter.ShouldSkip("app.pyc"));
  REQUIRE_FALSE(filter.ShouldSkip("src/main.cpp"));
  REQUIRE_FALSE(filter.ShouldSkip("README.md"));
}

TEST_CASE("comments and blank lines in .gitignore are ignored") {
  TempWorkspace ws;
  ws.WriteGitignore(
      "# this is a comment\n"
      "\n"
      "   \n"
      "*.o\n"
      "# another comment\n"
      "\n");
  GitignoreFilter filter(ws.Path());

  REQUIRE(filter.ShouldSkip("main.o"));
  REQUIRE_FALSE(filter.ShouldSkip("main.cpp"));
  REQUIRE_FALSE(filter.ShouldSkip("# this is a comment"));
}

TEST_CASE("ShouldSkip returns false for unmatched paths regardless of filter") {
  TempWorkspace ws;
  ws.WriteGitignore("node_modules/\n*.log\n");
  GitignoreFilter filter(ws.Path());

  REQUIRE_FALSE(filter.ShouldSkip("src/main.cpp"));
  REQUIRE_FALSE(filter.ShouldSkip("include/foo.hpp"));
  REQUIRE_FALSE(filter.ShouldSkip("CMakeLists.txt"));
  REQUIRE_FALSE(filter.ShouldSkip("README.md"));
}

TEST_CASE("extension patterns skip matching files anywhere in tree") {
  TempWorkspace ws;
  ws.WriteGitignore("*.pyc\n");
  GitignoreFilter filter(ws.Path());

  REQUIRE(filter.ShouldSkip("app.pyc"));
  REQUIRE(filter.ShouldSkip("lib/__pycache__/mod.pyc"));
  REQUIRE_FALSE(filter.ShouldSkip("app.py"));
}
