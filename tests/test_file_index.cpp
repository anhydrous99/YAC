#include "tool_call/file_index.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::tool_call::FileIndex;
using yac::tool_call::WorkspaceFilesystem;

namespace {

using namespace std::chrono_literals;

constexpr auto kReadyTimeout = 5s;

class TempWorkspace {
 public:
  TempWorkspace() {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("yac_test_file_index_" + std::to_string(counter.fetch_add(1)));
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

  void WriteFile(const std::filesystem::path& relative,
                 const std::string& content) const {
    const auto full = path_ / relative;
    std::filesystem::create_directories(full.parent_path());
    std::ofstream f(full);
    f << content;
  }

  void WriteGitignore(const std::string& content) const {
    std::ofstream f(path_ / ".gitignore");
    f << content;
  }

 private:
  std::filesystem::path path_;
};

bool Contains(const std::vector<std::string>& paths, const std::string& p) {
  return std::ranges::find(paths, p) != paths.end();
}

std::vector<std::string> Paths(
    const std::vector<yac::tool_call::FileMentionRow>& rows) {
  std::vector<std::string> out;
  out.reserve(rows.size());
  for (const auto& r : rows) {
    out.push_back(r.relative_path);
  }
  return out;
}

// Helper: kick off the warm and block the test thread until rows are
// published. UI code uses SetOnRebuildComplete instead — see bootstrap.cpp.
void WarmAndWait(FileIndex& index) {
  index.WarmAsync();
  REQUIRE(index.WaitForReady(kReadyTimeout));
}

}  // namespace

TEST_CASE("FileIndex excludes gitignored files and directories") {
  TempWorkspace ws;
  ws.WriteGitignore("ignored/\n*.log\n");
  ws.WriteFile("src/main.cpp", "int main(){}");
  ws.WriteFile("src/util.cpp", "");
  ws.WriteFile("ignored/junk.txt", "x");
  ws.WriteFile("trace.log", "x");

  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);
  WarmAndWait(index);

  const auto paths = Paths(index.Query("", 100));
  REQUIRE(Contains(paths, "src/main.cpp"));
  REQUIRE(Contains(paths, "src/util.cpp"));
  REQUIRE_FALSE(Contains(paths, "ignored/junk.txt"));
  REQUIRE_FALSE(Contains(paths, "trace.log"));
}

TEST_CASE("FileIndex with empty prefix sorts shorter paths first") {
  TempWorkspace ws;
  ws.WriteFile("a.txt", "");
  ws.WriteFile("sub/aaaa.txt", "");
  ws.WriteFile("bb.txt", "");

  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);
  WarmAndWait(index);

  const auto rows = index.Query("", 100);
  REQUIRE(rows.size() == 3);
  REQUIRE(rows[0].relative_path == "a.txt");
  REQUIRE(rows[1].relative_path == "bb.txt");
  REQUIRE(rows[2].relative_path == "sub/aaaa.txt");
}

TEST_CASE("FileIndex scores basename-prefix > path-prefix > substring") {
  TempWorkspace ws;
  ws.WriteFile("zzz/foo.cpp", "");
  ws.WriteFile("foo/bar.cpp", "");
  ws.WriteFile("src/xfoo.cpp", "");

  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);
  WarmAndWait(index);

  const auto rows = index.Query("foo", 10);
  REQUIRE(rows.size() == 3);
  REQUIRE(rows[0].relative_path == "zzz/foo.cpp");
  REQUIRE(rows[1].relative_path == "foo/bar.cpp");
  REQUIRE(rows[2].relative_path == "src/xfoo.cpp");
}

TEST_CASE("FileIndex queries are case-insensitive") {
  TempWorkspace ws;
  ws.WriteFile("README.md", "");

  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);
  WarmAndWait(index);

  REQUIRE(index.Query("read", 10).size() == 1);
  REQUIRE(index.Query("RE", 10).size() == 1);
  REQUIRE(index.Query("xyz", 10).empty());
}

TEST_CASE("FileIndex honors the result limit") {
  TempWorkspace ws;
  for (int i = 0; i < 50; ++i) {
    ws.WriteFile("f" + std::to_string(i) + ".txt", "");
  }

  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);
  WarmAndWait(index);

  REQUIRE(index.Query("", 5).size() == 5);
  REQUIRE(index.Query("", 100).size() == 50);
}

TEST_CASE("FileIndex Invalidate forces a rebuild") {
  TempWorkspace ws;
  ws.WriteFile("first.txt", "");
  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);
  WarmAndWait(index);

  REQUIRE(index.Query("", 100).size() == 1);

  ws.WriteFile("second.txt", "");
  index.Invalidate();
  REQUIRE(index.WaitForReady(kReadyTimeout));
  // After Invalidate the worker eventually publishes a fresh snapshot. Wait
  // up to kReadyTimeout for the rebuild that includes second.txt — a single
  // WaitForReady can return on the still-Ready pre-Invalidate state.
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(5);
  while (index.Query("", 100).size() != 2 &&
         std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  REQUIRE(index.Query("", 100).size() == 2);
}

TEST_CASE("FileIndex returns empty results for an empty workspace") {
  TempWorkspace ws;
  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);
  WarmAndWait(index);

  REQUIRE(index.Query("anything", 10).empty());
  REQUIRE(index.Query("", 10).empty());
}

TEST_CASE("FileIndex records file sizes") {
  TempWorkspace ws;
  ws.WriteFile("a.txt", "hello");
  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);
  WarmAndWait(index);

  const auto rows = index.Query("a", 10);
  REQUIRE(rows.size() == 1);
  REQUIRE(rows[0].size_bytes == 5);
}

TEST_CASE("FileIndex Query with limit 0 returns empty results") {
  TempWorkspace ws;
  ws.WriteFile("a.txt", "");
  ws.WriteFile("b.txt", "");
  ws.WriteFile("c.txt", "");
  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);
  WarmAndWait(index);

  REQUIRE(index.Query("", 0).empty());
  REQUIRE(index.Query("a", 0).empty());
}

TEST_CASE("FileIndex Query before warm returns empty without blocking") {
  TempWorkspace ws;
  ws.WriteFile("a.txt", "");
  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);

  const auto start = std::chrono::steady_clock::now();
  const auto rows = index.Query("a", 10);
  const auto elapsed = std::chrono::steady_clock::now() - start;

  REQUIRE(rows.empty());
  REQUIRE(index.GetState() == FileIndex::State::Cold);
  REQUIRE(elapsed < std::chrono::milliseconds(50));
}

TEST_CASE("FileIndex SetOnRebuildComplete fires after WarmAsync completes") {
  TempWorkspace ws;
  ws.WriteFile("only.txt", "");
  WorkspaceFilesystem fs(ws.Path());
  FileIndex index(fs);

  std::atomic<int> calls{0};
  index.SetOnRebuildComplete([&calls] { calls.fetch_add(1); });
  index.WarmAsync();
  REQUIRE(index.WaitForReady(kReadyTimeout));

  // The callback runs on the worker thread immediately after WaitForReady's
  // condition is satisfied. Give the worker a moment to invoke it.
  const auto deadline =
      std::chrono::steady_clock::now() + std::chrono::seconds(2);
  while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline) {
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  REQUIRE(calls.load() >= 1);
}

TEST_CASE("FileIndex destructor cancels an in-flight warm") {
  TempWorkspace ws;
  // Populate enough files that the walk takes non-trivial time even with rg.
  for (int i = 0; i < 2000; ++i) {
    ws.WriteFile("dir" + std::to_string(i / 100) + "/file" +
                     std::to_string(i) + ".txt",
                 "x");
  }
  WorkspaceFilesystem fs(ws.Path());

  const auto start = std::chrono::steady_clock::now();
  {
    FileIndex index(fs);
    index.WarmAsync();
    // Destruct without waiting — the destructor must request_stop the worker
    // and (if the walk is rg-based) kill the subprocess via the stop_token.
  }
  const auto elapsed = std::chrono::steady_clock::now() - start;
  // The walk itself should be sub-second on 2k files; even with stop signaling
  // the destructor must return well before the 5s rg timeout. Generous bound
  // to avoid flakes on slow CI.
  REQUIRE(elapsed < std::chrono::seconds(3));
}
