#include "cli/mcp_admin_command.hpp"
#include "mcp/token_store.hpp"
#include "presentation/mcp/mcp_slash_commands.hpp"
#include "presentation/slash_command_registry.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include <catch2/catch_test_macros.hpp>

namespace {

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f << content;
}

class FakeTokenStore : public yac::mcp::ITokenStore {
 public:
  [[nodiscard]] std::optional<std::string> Get(
      std::string_view /*server_id*/) const override {
    return std::nullopt;
  }
  void Set(std::string_view /*server_id*/,
           std::string_view /*token_json*/) override {}
  void Erase(std::string_view /*server_id*/) override {}
};

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

}  // namespace

TEST_CASE("list_renders") {
  TempDir tmp("yac_test_slash_mcp_list");
  const auto toml_path = tmp.Path() / "settings.toml";

  WriteFile(toml_path,
            "[[mcp.servers]]\n"
            "id = \"my-server\"\ntransport = \"stdio\"\ncommand = \"npx\"\n"
            "[[mcp.servers]]\n"
            "id = \"remote-server\"\ntransport = \"http\"\n"
            "url = \"https://example.com/mcp\"\n");

  yac::cli::McpAdminCommand::Options opts;
  opts.settings_path = toml_path;
  opts.token_store = std::make_shared<FakeTokenStore>();
  auto mcp_admin = std::make_shared<yac::cli::McpAdminCommand>(std::move(opts));

  yac::presentation::SlashCommandRegistry registry;
  yac::presentation::RegisterMcpSlashCommands(registry);

  std::string snapshot;
  registry.SetArgumentsHandler("mcp", [&](std::string args) {
    if (args == "list") {
      const auto servers = mcp_admin->ListServers();
      for (const auto& s : servers) {
        snapshot += s.id + "  [" + s.state + "]  " + s.transport + "\n";
      }
    }
  });

  REQUIRE(registry.TryDispatch("/mcp list"));
  REQUIRE(snapshot.find("my-server") != std::string::npos);
  REQUIRE(snapshot.find("stdio") != std::string::npos);
  REQUIRE(snapshot.find("remote-server") != std::string::npos);
  REQUIRE(snapshot.find("http") != std::string::npos);
}

TEST_CASE("auth_non_blocking") {
  yac::presentation::SlashCommandRegistry registry;
  yac::presentation::RegisterMcpSlashCommands(registry);

  std::atomic<bool> auth_done{false};

  registry.SetArgumentsHandler("mcp", [&](std::string args) {
    if (args.starts_with("auth")) {
      std::thread([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        auth_done = true;
      }).detach();
    }
  });

  const auto start = std::chrono::steady_clock::now();
  REQUIRE(registry.TryDispatch("/mcp auth some-server"));
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  REQUIRE(elapsed < 25);
  REQUIRE_FALSE(auth_done.load());
}
