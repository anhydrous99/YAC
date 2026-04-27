#include "cli/mcp_cli_dispatch.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

class TempDir {
 public:
  explicit TempDir(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
    std::filesystem::create_directories(path_, ec);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f << content;
}

}  // namespace

TEST_CASE("list_shows_servers") {
  TempDir tmp("yac_test_mcp_cli_list");
  const auto toml_path = tmp.Path() / "settings.toml";
  WriteFile(toml_path,
            "[[mcp.servers]]\nid = \"my-server\"\ntransport = \"stdio\"\n"
            "command = \"npx\"\n");

  std::ostringstream out;
  std::ostringstream err;

  std::vector<std::string> args = {"list"};
  std::vector<char*> argv;
  for (auto& s : args) {
    argv.push_back(s.data());
  }

  yac::cli::McpCliOptions opts;
  opts.settings_path = toml_path;
  opts.out = &out;
  opts.err = &err;

  const int rc = yac::cli::RunMcpCli(static_cast<int>(argv.size()), argv.data(),
                                     std::move(opts));

  REQUIRE(rc == 0);
  REQUIRE(out.str().find("my-server") != std::string::npos);
}

TEST_CASE("bad_args_exit_1") {
  std::ostringstream out;
  std::ostringstream err;

  std::vector<std::string> args = {"foo"};
  std::vector<char*> argv;
  for (auto& s : args) {
    argv.push_back(s.data());
  }

  yac::cli::McpCliOptions opts;
  opts.out = &out;
  opts.err = &err;

  const int rc = yac::cli::RunMcpCli(static_cast<int>(argv.size()), argv.data(),
                                     std::move(opts));

  REQUIRE(rc == 1);
  REQUIRE(err.str().find("unknown subcommand") != std::string::npos);
}
