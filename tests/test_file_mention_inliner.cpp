#include "presentation/file_mention_inliner.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <string>

#include <catch2/catch_test_macros.hpp>

using yac::presentation::InlineFileMentions;
using yac::tool_call::WorkspaceFilesystem;

namespace {

class TempWorkspace {
 public:
  TempWorkspace() {
    static std::atomic<int> counter{0};
    path_ = std::filesystem::temp_directory_path() /
            ("yac_test_inliner_" + std::to_string(counter.fetch_add(1)));
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
    std::ofstream f(full, std::ios::binary);
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
  }

 private:
  std::filesystem::path path_;
};

}  // namespace

TEST_CASE("InlineFileMentions returns input unchanged when no @ tokens") {
  TempWorkspace ws;
  WorkspaceFilesystem fs(ws.Path());
  const auto result = InlineFileMentions("hello world", fs);
  REQUIRE(result.text == "hello world");
  REQUIRE(result.diagnostics.empty());
}

TEST_CASE("InlineFileMentions attaches a single file's contents") {
  TempWorkspace ws;
  ws.WriteFile("foo.txt", "hello");
  WorkspaceFilesystem fs(ws.Path());

  const auto result = InlineFileMentions("look at @foo.txt", fs);
  REQUIRE(result.text.starts_with("look at @foo.txt"));
  REQUIRE(result.text.find("Attached files:") != std::string::npos);
  REQUIRE(result.text.find("--- BEGIN @foo.txt ---") != std::string::npos);
  REQUIRE(result.text.find("hello") != std::string::npos);
  REQUIRE(result.text.find("--- END @foo.txt ---") != std::string::npos);
  REQUIRE(result.diagnostics.empty());
}

TEST_CASE("InlineFileMentions emits an error block for a missing file") {
  TempWorkspace ws;
  WorkspaceFilesystem fs(ws.Path());
  const auto result = InlineFileMentions("see @missing.txt", fs);
  REQUIRE(result.text.find("[error: file not found]") != std::string::npos);
  REQUIRE(result.diagnostics.size() == 1);
  REQUIRE(result.diagnostics[0].path == "missing.txt");
}

TEST_CASE("InlineFileMentions reports paths outside the workspace as errors") {
  TempWorkspace ws;
  WorkspaceFilesystem fs(ws.Path());
  const auto result = InlineFileMentions("see @../outside", fs);
  REQUIRE(result.text.find("[error:") != std::string::npos);
  REQUIRE(result.diagnostics.size() == 1);
}

TEST_CASE("InlineFileMentions reports directories as errors") {
  TempWorkspace ws;
  std::filesystem::create_directories(ws.Path() / "subdir");
  WorkspaceFilesystem fs(ws.Path());
  const auto result = InlineFileMentions("see @subdir", fs);
  REQUIRE(result.text.find("[error: is a directory]") != std::string::npos);
}

TEST_CASE("InlineFileMentions detects binary files") {
  TempWorkspace ws;
  ws.WriteFile("bin.dat", std::string("hello\0world", 11));
  WorkspaceFilesystem fs(ws.Path());
  const auto result = InlineFileMentions("see @bin.dat", fs);
  REQUIRE(result.text.find("[error: binary file]") != std::string::npos);
}

TEST_CASE("InlineFileMentions truncates content past per_file_cap") {
  TempWorkspace ws;
  ws.WriteFile("big.txt", std::string(200, 'x'));
  WorkspaceFilesystem fs(ws.Path());
  const auto result =
      InlineFileMentions("see @big.txt", fs, /*per_file_cap=*/100);
  REQUIRE(result.text.find("[truncated: 100 bytes omitted]") !=
          std::string::npos);
}

TEST_CASE("InlineFileMentions halts when total_cap is exhausted") {
  TempWorkspace ws;
  for (int i = 0; i < 5; ++i) {
    ws.WriteFile("f" + std::to_string(i) + ".txt", std::string(100, 'x'));
  }
  WorkspaceFilesystem fs(ws.Path());
  const auto result =
      InlineFileMentions("see @f0.txt @f1.txt @f2.txt @f3.txt @f4.txt", fs,
                         /*per_file_cap=*/200, /*total_cap=*/250);
  REQUIRE(result.text.find("remaining files skipped") != std::string::npos);
}

TEST_CASE("InlineFileMentions dedupes repeated tokens") {
  TempWorkspace ws;
  ws.WriteFile("foo.txt", "x");
  WorkspaceFilesystem fs(ws.Path());
  const auto result = InlineFileMentions("@foo.txt @foo.txt @foo.txt", fs);
  const auto first = result.text.find("--- BEGIN @foo.txt ---");
  REQUIRE(first != std::string::npos);
  REQUIRE(result.text.find("--- BEGIN @foo.txt ---", first + 1) ==
          std::string::npos);
}

TEST_CASE("InlineFileMentions ignores tokens not preceded by whitespace") {
  TempWorkspace ws;
  ws.WriteFile("host.com", "x");
  WorkspaceFilesystem fs(ws.Path());
  const auto result = InlineFileMentions("email me@host.com please", fs);
  REQUIRE(result.text == "email me@host.com please");
  REQUIRE(result.diagnostics.empty());
}

TEST_CASE("InlineFileMentions triggers at line start and after whitespace") {
  TempWorkspace ws;
  ws.WriteFile("a.txt", "A");
  ws.WriteFile("b.txt", "B");
  WorkspaceFilesystem fs(ws.Path());
  const auto result = InlineFileMentions("@a.txt and @b.txt", fs);
  REQUIRE(result.text.find("--- BEGIN @a.txt ---") != std::string::npos);
  REQUIRE(result.text.find("--- BEGIN @b.txt ---") != std::string::npos);
}

TEST_CASE("InlineFileMentions handles user text with only a mention") {
  TempWorkspace ws;
  ws.WriteFile("only.txt", "ONE");
  WorkspaceFilesystem fs(ws.Path());
  const auto result = InlineFileMentions("@only.txt", fs);
  REQUIRE(result.text.starts_with("@only.txt"));
  REQUIRE(result.text.find("ONE") != std::string::npos);
}

TEST_CASE(
    "InlineFileMentions reports full-file truncation count without "
    "loading whole file") {
  // Truncation message must report bytes-omitted against the file's *real*
  // size, not the bounded read buffer. A regression where the inliner
  // reverts to ReadFile + content.size() arithmetic would still pass the
  // smaller per_file_cap=100 test above, but would compute "0 bytes
  // omitted" here because content.size() == per_file_cap + sniff.
  constexpr std::size_t kFullSize = 2UL * 1024UL * 1024UL;
  constexpr std::size_t kPerFileCap = 1024;
  TempWorkspace ws;
  ws.WriteFile("big.txt", std::string(kFullSize, 'x'));
  WorkspaceFilesystem fs(ws.Path());

  const auto result = InlineFileMentions("see @big.txt", fs, kPerFileCap);

  const auto expected =
      "[truncated: " + std::to_string(kFullSize - kPerFileCap) +
      " bytes omitted]";
  REQUIRE(result.text.find(expected) != std::string::npos);
}
