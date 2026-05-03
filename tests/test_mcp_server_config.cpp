#include "chat/settings_toml.hpp"
#include "chat/settings_toml_template.hpp"
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
using yac::chat::kDefaultSettingsToml;
using yac::chat::LoadSettingsFromToml;
using yac::mcp::McpAuthBearer;
using yac::mcp::McpAuthOAuth;

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

std::filesystem::path FindRepoFile(std::string_view relative_path) {
  auto current = std::filesystem::current_path();
  for (int depth = 0; depth < 8; ++depth) {
    auto candidate = current / relative_path;
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
    if (!current.has_parent_path() || current == current.parent_path()) {
      break;
    }
    current = current.parent_path();
  }
  return {};
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

TEST_CASE("parses_http_with_nested_bearer_auth") {
  TempFile file("yac_test_mcp_http_nested_bearer.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"myserver\"\n"
            "transport = \"http\"\n"
            "url = \"https://example.com/mcp\"\n"
            "\n"
            "[mcp.servers.auth.bearer]\n"
            "api_key_env = \"MY_API_KEY\"\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  auto fields = LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(config.mcp.servers.size() == 1);
  const auto& srv = config.mcp.servers[0];
  REQUIRE(srv.auth.has_value());
  const auto* bearer = std::get_if<McpAuthBearer>(&srv.auth.value());
  REQUIRE(bearer != nullptr);
  REQUIRE(bearer->api_key_env == "MY_API_KEY");
  REQUIRE(fields.mcp_servers.at("myserver").api_key_env);
}

TEST_CASE("parses_http_with_legacy_oauth_auth") {
  TempFile file("yac_test_mcp_http_legacy_oauth.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"oauth-server\"\n"
            "transport = \"http\"\n"
            "url = \"https://example.com/mcp\"\n"
            "\n"
            "[mcp.servers.auth]\n"
            "type = \"oauth\"\n"
            "authorization_url = \"https://auth.example.com/authorize\"\n"
            "token_url = \"https://auth.example.com/token\"\n"
            "client_id = \"client-id\"\n"
            "scopes = [\"read\", \"write\"]\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(config.mcp.servers.size() == 1);
  const auto& srv = config.mcp.servers[0];
  REQUIRE(srv.auth.has_value());
  const auto* oauth = std::get_if<McpAuthOAuth>(&srv.auth.value());
  REQUIRE(oauth != nullptr);
  REQUIRE(oauth->authorization_url == "https://auth.example.com/authorize");
  REQUIRE(oauth->token_url == "https://auth.example.com/token");
  REQUIRE(oauth->client_id == "client-id");
  REQUIRE(oauth->scopes == std::vector<std::string>{"read", "write"});
}

TEST_CASE("parses_http_with_nested_oauth2_auth") {
  TempFile file("yac_test_mcp_http_nested_oauth2.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"example-oauth\"\n"
            "transport = \"http\"\n"
            "url = \"https://mcp.example.com/api\"\n"
            "enabled = true\n"
            "auto_start = false\n"
            "\n"
            "[mcp.servers.auth.oauth2]\n"
            "authorization_url = \"https://auth.example.com/authorize\"\n"
            "token_url = \"https://auth.example.com/token\"\n"
            "client_id = \"your-client-id\"\n"
            "scopes = [\"read\", \"write\"]\n");
  ChatConfig config;
  std::vector<ConfigIssue> issues;
  LoadSettingsFromToml(file.Path(), config, issues);
  REQUIRE(issues.empty());
  REQUIRE(config.mcp.servers.size() == 1);
  const auto& srv = config.mcp.servers[0];
  REQUIRE_FALSE(srv.auto_start);
  REQUIRE(srv.auth.has_value());
  const auto* oauth = std::get_if<McpAuthOAuth>(&srv.auth.value());
  REQUIRE(oauth != nullptr);
  REQUIRE(oauth->authorization_url == "https://auth.example.com/authorize");
  REQUIRE(oauth->token_url == "https://auth.example.com/token");
  REQUIRE(oauth->client_id == "your-client-id");
  REQUIRE(oauth->scopes == std::vector<std::string>{"read", "write"});
}

TEST_CASE("settings examples parse without MCP auth schema errors") {
  TempFile generated("yac_test_mcp_generated_settings.toml");
  WriteFile(generated.Path(), kDefaultSettingsToml);

  ChatConfig generated_config;
  std::vector<ConfigIssue> generated_issues;
  LoadSettingsFromToml(generated.Path(), generated_config, generated_issues);
  REQUIRE(generated_issues.empty());

  const auto example_path = FindRepoFile("settings.example.toml");
  REQUIRE_FALSE(example_path.empty());
  ChatConfig example_config;
  std::vector<ConfigIssue> example_issues;
  LoadSettingsFromToml(example_path, example_config, example_issues);
  REQUIRE(example_issues.empty());
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
