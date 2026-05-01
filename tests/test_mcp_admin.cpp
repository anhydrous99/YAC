#include "cli/mcp_admin_command.hpp"
#include "mcp/mcp_server_config.hpp"
#include "mcp/token_store.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

#include <catch2/catch_test_macros.hpp>

namespace {

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream f(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(f), std::istreambuf_iterator<char>()};
}

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f << content;
}

class FakeTokenStore : public yac::mcp::ITokenStore {
 public:
  [[nodiscard]] std::optional<std::string> Get(
      std::string_view server_id) const override {
    const auto it = store_.find(std::string(server_id));
    if (it == store_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  void Set(std::string_view server_id, std::string_view token_json) override {
    store_[std::string(server_id)] = std::string(token_json);
  }

  void Erase(std::string_view server_id) override {
    store_.erase(std::string(server_id));
  }

 private:
  std::unordered_map<std::string, std::string> store_;
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

TEST_CASE("add_preserves_comments") {
  TempDir tmp("yac_test_mcp_admin_add");
  const auto toml_path = tmp.Path() / "settings.toml";

  constexpr std::string_view kInitialToml =
      "# My config file\n"
      "temperature = 0.7\n"
      "\n"
      "[provider]\n"
      "# provider comment\n"
      "id = \"openai-compatible\"\n";

  WriteFile(toml_path, kInitialToml);

  yac::cli::McpAdminCommand::Options opts;
  opts.settings_path = toml_path;
  opts.token_store = std::make_shared<FakeTokenStore>();
  yac::cli::McpAdminCommand cmd(std::move(opts));

  yac::mcp::McpServerConfig cfg;
  cfg.id = "test-srv";
  cfg.transport = "stdio";
  cfg.command = "npx";
  cfg.args = {"-y", "my-mcp-package"};
  cfg.enabled = true;

  cmd.AddServer(cfg);

  const std::string result = ReadFile(toml_path);

  REQUIRE(result.find("# My config file") != std::string::npos);
  REQUIRE(result.find("# provider comment") != std::string::npos);
  REQUIRE(result.find("[[mcp.servers]]") != std::string::npos);
  REQUIRE(result.find("id = \"test-srv\"") != std::string::npos);
  REQUIRE(result.find("transport = \"stdio\"") != std::string::npos);
  REQUIRE(result.find("command = \"npx\"") != std::string::npos);
}

TEST_CASE("add_rejects_duplicate_id") {
  TempDir tmp("yac_test_mcp_admin_dup");
  const auto toml_path = tmp.Path() / "settings.toml";

  WriteFile(toml_path,
            "[[mcp.servers]]\nid = \"dup\"\ntransport = \"stdio\"\n"
            "command = \"x\"\n");

  yac::cli::McpAdminCommand::Options opts;
  opts.settings_path = toml_path;
  opts.token_store = std::make_shared<FakeTokenStore>();
  yac::cli::McpAdminCommand cmd(std::move(opts));

  yac::mcp::McpServerConfig cfg;
  cfg.id = "dup";
  cfg.transport = "stdio";
  cfg.command = "y";

  REQUIRE_THROWS_AS(cmd.AddServer(cfg), std::runtime_error);
}

TEST_CASE("debug_output") {
  TempDir tmp("yac_test_mcp_admin_debug");
  const auto toml_path = tmp.Path() / "settings.toml";

  constexpr std::string_view kInitialToml =
      "[[mcp.servers]]\n"
      "id = \"debug-srv\"\n"
      "transport = \"http\"\n"
      "url = \"https://example.com/mcp\"\n";

  WriteFile(toml_path, kInitialToml);

  const auto fake_store = std::make_shared<FakeTokenStore>();
  fake_store->Set(
      "debug-srv",
      R"({"access_token":"secrettoken_abc123","refresh_token":"ref",)"
      R"("expires_at":4102444800,"token_type":"Bearer","scope":""})");

  const auto log_path = tmp.Path() / "mcp_debug.log";
  {
    std::ofstream lf(log_path);
    for (int i = 1; i <= 60; ++i) {
      lf << "line " << i << "\n";
    }
  }

  yac::cli::McpAdminCommand::Options opts;
  opts.settings_path = toml_path;
  opts.token_store = fake_store;
  opts.connectivity_test = [](const yac::mcp::McpServerConfig& c) -> bool {
    return c.transport == "http";
  };
  yac::cli::McpAdminCommand cmd(std::move(opts));

  const auto report = cmd.Debug("debug-srv");

  REQUIRE(report.server_id == "debug-srv");

  REQUIRE(report.status.find("debug-srv") != std::string::npos);
  REQUIRE(report.status.find("transport") != std::string::npos);

  REQUIRE(report.auth.find("token: present") != std::string::npos);
  REQUIRE(report.auth.find("expiry") != std::string::npos);
  REQUIRE(report.auth.find("expires in") != std::string::npos);
  REQUIRE(report.auth.find("secrettoken_abc123") == std::string::npos);

  REQUIRE(report.connectivity.find("PASS") != std::string::npos);
}

TEST_CASE("debug_redacts_secrets_in_log") {
  TempDir tmp("yac_test_mcp_admin_redact");
  const auto toml_path = tmp.Path() / "settings.toml";

  WriteFile(toml_path,
            "[[mcp.servers]]\nid = \"redact-srv\"\ntransport = \"stdio\"\n"
            "command = \"x\"\n");

  const auto fake_store = std::make_shared<FakeTokenStore>();

  const auto log_path = tmp.Path() / "mcp_debug_redact.log";
  {
    std::ofstream lf(log_path);
    lf << R"({"Authorization":"Bearer supersecrettoken123"})" << "\n";
  }

  yac::cli::McpAdminCommand::Options opts;
  opts.settings_path = toml_path;
  opts.token_store = fake_store;
  opts.connectivity_test = [](const yac::mcp::McpServerConfig&) {
    return true;
  };
  yac::cli::McpAdminCommand cmd(std::move(opts));

  const auto report = cmd.Debug("redact-srv");

  REQUIRE(report.log.find("supersecrettoken123") == std::string::npos);
}

TEST_CASE("logout_clears_token") {
  TempDir tmp("yac_test_mcp_admin_logout");
  const auto toml_path = tmp.Path() / "settings.toml";

  WriteFile(toml_path,
            "[[mcp.servers]]\nid = \"logout-srv\"\ntransport = \"stdio\"\n"
            "command = \"x\"\n");

  const auto fake_store = std::make_shared<FakeTokenStore>();
  fake_store->Set("logout-srv", R"({"access_token":"abc"})");

  yac::cli::McpAdminCommand::Options opts;
  opts.settings_path = toml_path;
  opts.token_store = fake_store;
  yac::cli::McpAdminCommand cmd(std::move(opts));

  cmd.Logout("logout-srv");

  REQUIRE_FALSE(fake_store->Get("logout-srv").has_value());
}

TEST_CASE("list_servers_returns_configured_entries") {
  TempDir tmp("yac_test_mcp_admin_list");
  const auto toml_path = tmp.Path() / "settings.toml";

  WriteFile(toml_path,
            "[[mcp.servers]]\nid = \"srv-a\"\ntransport = \"stdio\"\n"
            "command = \"a\"\n"
            "[[mcp.servers]]\nid = \"srv-b\"\ntransport = \"http\"\n"
            "url = \"https://example.com\"\n");

  yac::cli::McpAdminCommand::Options opts;
  opts.settings_path = toml_path;
  opts.token_store = std::make_shared<FakeTokenStore>();
  yac::cli::McpAdminCommand cmd(std::move(opts));

  const auto statuses = cmd.ListServers();
  REQUIRE(statuses.size() == 2);
  REQUIRE(statuses[0].id == "srv-a");
  REQUIRE(statuses[0].state == "configured");
  REQUIRE(statuses[1].id == "srv-b");
}
