#include "tool_call/workspace_walker.hpp"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <stop_token>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::tool_call::SetRipgrepDisabledForTesting;
using yac::tool_call::WalkResult;
using yac::tool_call::WalkWorkspace;

namespace {

class TempWorkspace {
 public:
  TempWorkspace() {
    static std::atomic<int> counter{0};
    path_ =
        std::filesystem::temp_directory_path() /
        ("yac_test_workspace_walker_" + std::to_string(counter.fetch_add(1)));
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

// RAII guard so a test that disables rg leaves the global flag clean even
// if a REQUIRE fires partway through.
struct RipgrepDisableGuard {
  RipgrepDisableGuard() { SetRipgrepDisabledForTesting(true); }
  ~RipgrepDisableGuard() { SetRipgrepDisabledForTesting(false); }
  RipgrepDisableGuard(const RipgrepDisableGuard&) = delete;
  RipgrepDisableGuard& operator=(const RipgrepDisableGuard&) = delete;
  RipgrepDisableGuard(RipgrepDisableGuard&&) = delete;
  RipgrepDisableGuard& operator=(RipgrepDisableGuard&&) = delete;
};

std::vector<std::string> Paths(const WalkResult& r) {
  std::vector<std::string> out;
  out.reserve(r.rows.size());
  for (const auto& row : r.rows) {
    out.push_back(row.relative_path);
  }
  return out;
}

}  // namespace

TEST_CASE("WalkWorkspace returns sorted relative paths") {
  TempWorkspace ws;
  ws.WriteFile("a.txt", "");
  ws.WriteFile("zz/bbb.txt", "");
  ws.WriteFile("bb.txt", "");

  const auto result =
      WalkWorkspace(ws.Path(), /*max_entries=*/100, std::stop_token{});

  const auto paths = Paths(result);
  REQUIRE(paths.size() == 3);
  // Shorter path first, then lexicographic.
  REQUIRE(paths[0] == "a.txt");
  REQUIRE(paths[1] == "bb.txt");
  REQUIRE(paths[2] == "zz/bbb.txt");
}

TEST_CASE("WalkWorkspace populates file sizes") {
  TempWorkspace ws;
  ws.WriteFile("hello.txt", "12345");

  const auto result =
      WalkWorkspace(ws.Path(), /*max_entries=*/10, std::stop_token{});

  REQUIRE(result.rows.size() == 1);
  REQUIRE(result.rows[0].size_bytes == 5);
}

TEST_CASE("WalkWorkspace caps results at max_entries") {
  TempWorkspace ws;
  for (int i = 0; i < 20; ++i) {
    ws.WriteFile("f" + std::to_string(i) + ".txt", "");
  }

  const auto result =
      WalkWorkspace(ws.Path(), /*max_entries=*/5, std::stop_token{});

  REQUIRE(result.rows.size() == 5);
}

TEST_CASE("WalkWorkspace rg path honors .gitignore in a non-git workspace") {
  TempWorkspace ws;
  ws.WriteGitignore("ignored/\n*.log\n");
  ws.WriteFile("src/main.cpp", "");
  ws.WriteFile("ignored/junk.txt", "");
  ws.WriteFile("trace.log", "");

  const auto result =
      WalkWorkspace(ws.Path(), /*max_entries=*/100, std::stop_token{});

  const auto paths = Paths(result);
  REQUIRE(std::ranges::find(paths, std::string("src/main.cpp")) != paths.end());
  REQUIRE(std::ranges::find(paths, std::string("ignored/junk.txt")) ==
          paths.end());
  REQUIRE(std::ranges::find(paths, std::string("trace.log")) == paths.end());
}

TEST_CASE("WalkWorkspace fallback walker honors .gitignore") {
  RipgrepDisableGuard guard;
  TempWorkspace ws;
  ws.WriteGitignore("ignored/\n*.log\n");
  ws.WriteFile("src/main.cpp", "");
  ws.WriteFile("ignored/junk.txt", "");
  ws.WriteFile("trace.log", "");

  const auto result =
      WalkWorkspace(ws.Path(), /*max_entries=*/100, std::stop_token{});

  REQUIRE_FALSE(result.used_rg);
  const auto paths = Paths(result);
  REQUIRE(std::ranges::find(paths, std::string("src/main.cpp")) != paths.end());
  REQUIRE(std::ranges::find(paths, std::string("ignored/junk.txt")) ==
          paths.end());
  REQUIRE(std::ranges::find(paths, std::string("trace.log")) == paths.end());
}

TEST_CASE("WalkWorkspace fallback walker caps at max_entries") {
  RipgrepDisableGuard guard;
  TempWorkspace ws;
  for (int i = 0; i < 20; ++i) {
    ws.WriteFile("f" + std::to_string(i) + ".txt", "");
  }

  const auto result =
      WalkWorkspace(ws.Path(), /*max_entries=*/5, std::stop_token{});

  REQUIRE_FALSE(result.used_rg);
  REQUIRE(result.rows.size() == 5);
  REQUIRE(result.truncated);
}

TEST_CASE("WalkWorkspace returns cancelled when stop_token is already set") {
  TempWorkspace ws;
  ws.WriteFile("a.txt", "");

  std::stop_source src;
  src.request_stop();

  const auto result =
      WalkWorkspace(ws.Path(), /*max_entries=*/100, src.get_token());

  // Either rg saw the token and bailed, or the fallback walker did. In both
  // cases the result is flagged cancelled and the rows vector is left empty.
  REQUIRE(result.cancelled);
  REQUIRE(result.rows.empty());
}
