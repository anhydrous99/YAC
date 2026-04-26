#include "mcp/debug_log.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

#include <catch2/catch_test_macros.hpp>

namespace {

class ScopedHome {
 public:
  explicit ScopedHome(const std::filesystem::path& value) {
    if (const char* prior = std::getenv("HOME")) {
      had_prior_ = true;
      prior_ = prior;
    }
    ::setenv("HOME", value.c_str(), 1);
  }

  ~ScopedHome() {
    if (had_prior_) {
      ::setenv("HOME", prior_.c_str(), 1);
    } else {
      ::unsetenv("HOME");
    }
  }

  ScopedHome(const ScopedHome&) = delete;
  ScopedHome& operator=(const ScopedHome&) = delete;
  ScopedHome(ScopedHome&&) = delete;
  ScopedHome& operator=(ScopedHome&&) = delete;

 private:
  bool had_prior_ = false;
  std::string prior_;
};

std::string ReadAll(const std::filesystem::path& path) {
  std::ifstream file(path);
  REQUIRE(file.is_open());
  return {std::istreambuf_iterator<char>(file),
          std::istreambuf_iterator<char>()};
}

}  // namespace

TEST_CASE("redacts_in_log", "[mcp_debug_log]") {
  const auto root =
      std::filesystem::temp_directory_path() / "yac-mcp-debug-log";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  REQUIRE_FALSE(ec);

  ScopedHome home(root);
  yac::mcp::McpDebugLog log("server/id");
  log.LogFrame("in", R"({"Authorization":"Bearer secret123"})");

  const auto path = root / ".yac" / "logs" / "mcp" / "server_id.log";
  const std::string contents = ReadAll(path);
  REQUIRE(contents.find("secret123") == std::string::npos);
  REQUIRE(contents.find("[REDACTED]") != std::string::npos);

#ifndef _WIN32
  const auto file_perms =
      std::filesystem::status(path).permissions() & std::filesystem::perms::all;
  REQUIRE(file_perms == (std::filesystem::perms::owner_read |
                         std::filesystem::perms::owner_write));
#endif
}

TEST_CASE("shutdown_sentinel", "[mcp_debug_log]") {
  const auto root =
      std::filesystem::temp_directory_path() / "yac-mcp-debug-log-2";
  std::error_code ec;
  std::filesystem::remove_all(root, ec);
  std::filesystem::create_directories(root, ec);
  REQUIRE_FALSE(ec);

  ScopedHome home(root);
  yac::mcp::McpDebugLog log("server");
  log.LogShutdown();

  const auto path = root / ".yac" / "logs" / "mcp" / "server.log";
  const std::string contents = ReadAll(path);
  REQUIRE(contents.find("connection_closed") != std::string::npos);
}
