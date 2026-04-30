#include "chat/settings_toml.hpp"
#include "chat/types.hpp"
#include "mcp/mcp_server_config.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::chat::ChatConfig;
using yac::chat::ConfigIssue;
using yac::chat::ConfigIssueSeverity;
using yac::chat::LoadSettingsFromToml;
using yac::mcp::McpAuthBearer;

namespace {

class TempFile {
 public:
  explicit TempFile(std::string_view name)
      : path_(std::filesystem::temp_directory_path() / name) {
    std::filesystem::remove_all(path_);
  }
  ~TempFile() { std::filesystem::remove_all(path_); }
  TempFile(const TempFile&) = delete;
  TempFile& operator=(const TempFile&) = delete;
  TempFile(TempFile&&) = delete;
  TempFile& operator=(TempFile&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

void WriteFile(const std::filesystem::path& path, std::string_view content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream stream(path, std::ios::trunc);
  stream << content;
}

}  // namespace

TEST_CASE("parses_minimal_stdio") {
  TempFile file("yac_test_mcp_stdio.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"stdio\"\n"
            "command = \"npx\"\n"
            "args = [\"-y\", \"@upstash/context7-mcp\"]\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(config.mcp.servers.size() == 1);
  const auto& srv = config.mcp.servers[0];
  REQUIRE(srv.id == "ctx7");
  REQUIRE(srv.transport == "stdio");
  REQUIRE(srv.command == "npx");
  REQUIRE(srv.auto_start == true);
}

TEST_CASE("rejects_duplicate_ids") {
  TempFile file("yac_test_mcp_dup.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"stdio\"\n"
            "command = \"npx\"\n"
            "\n"
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"stdio\"\n"
            "command = \"npx\"\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  const bool has_error =
      std::ranges::any_of(issues, [](const ConfigIssue& issue) {
        return issue.severity == ConfigIssueSeverity::Error &&
               (issue.message.find("duplicate") != std::string::npos ||
                issue.message.find("Duplicate") != std::string::npos ||
                issue.detail.find("duplicate") != std::string::npos);
      });
  REQUIRE(has_error);
  REQUIRE(config.mcp.servers.size() == 1);
  REQUIRE(config.mcp.servers[0].id == "ctx7");
}

TEST_CASE("parses_http_with_bearer") {
  TempFile file("yac_test_mcp_http_bearer.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"myserver\"\n"
            "transport = \"http\"\n"
            "url = \"https://example.com/mcp\"\n"
            "auth = {type = \"bearer\", api_key_env = \"MY_API_KEY\"}\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(config.mcp.servers.size() == 1);
  const auto& srv = config.mcp.servers[0];
  REQUIRE(srv.transport == "http");
  REQUIRE(srv.url == "https://example.com/mcp");
  bool has_bearer = false;
  if (srv.auth.has_value()) {
    if (const auto* b = std::get_if<McpAuthBearer>(&srv.auth.value())) {
      has_bearer = (b->api_key_env == "MY_API_KEY");
    }
  }
  REQUIRE(has_bearer);
}

TEST_CASE("preset_fill_in") {
  TempFile file("yac_test_mcp_preset.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"context7\"\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(config.mcp.servers.size() == 1);
  const auto& srv = config.mcp.servers[0];
  REQUIRE(srv.id == "context7");
  REQUIRE(srv.transport == "stdio");
  REQUIRE(srv.command == "npx");
  REQUIRE(srv.args == std::vector<std::string>{"-y", "@upstash/context7-mcp"});
}
