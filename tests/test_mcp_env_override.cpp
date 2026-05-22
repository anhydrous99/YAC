#include "chat/config.hpp"
#include "mcp/mcp_server_config.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <variant>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, std::string value) : name_(std::move(name)) {
    const char* previous = std::getenv(name_.c_str());
    if (previous != nullptr) {
      has_previous_ = true;
      previous_ = previous;
    }
    setenv(name_.c_str(), value.c_str(), 1);
  }

  ~ScopedEnvVar() {
    if (has_previous_) {
      setenv(name_.c_str(), previous_.c_str(), 1);
    } else {
      unsetenv(name_.c_str());
    }
  }

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;
  ScopedEnvVar(ScopedEnvVar&&) = delete;
  ScopedEnvVar& operator=(ScopedEnvVar&&) = delete;

 private:
  std::string name_;
  std::string previous_;
  bool has_previous_ = false;
};

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

bool HasError(const yac::chat::ChatConfigResult& result,
              std::string_view text) {
  return std::ranges::any_of(result.issues, [&](const auto& issue) {
    return issue.severity == yac::chat::ConfigIssueSeverity::Error &&
           (issue.message.find(text) != std::string::npos ||
            issue.detail.find(text) != std::string::npos);
  });
}

const yac::mcp::McpAuthBearer* BearerAuth(
    const yac::mcp::McpServerConfig& server) {
  if (!server.auth.has_value()) {
    return nullptr;
  }
  return std::get_if<yac::mcp::McpAuthBearer>(&server.auth.value());
}

const yac::mcp::McpAuthOAuth* OAuthAuth(
    const yac::mcp::McpServerConfig& server) {
  if (!server.auth.has_value()) {
    return nullptr;
  }
  return std::get_if<yac::mcp::McpAuthOAuth>(&server.auth.value());
}

}  // namespace

TEST_CASE("override_command") {
  TempFile file("yac_test_mcp_env_override.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"stdio\"\n"
            "command = \"npx\"\n"
            "args = [\"-y\", \"@upstash/context7-mcp\"]\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar override("YAC_MCP_CTX7_COMMAND", "custom-bin");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.issues.empty());
  REQUIRE(result.config.mcp.servers.size() == 1);
  REQUIRE(result.config.mcp.servers[0].command == "custom-bin");
}

TEST_CASE("unknown_server_ignored") {
  TempFile file("yac_test_mcp_env_unknown.toml");
  WriteFile(file.Path(), "temperature = 0.5\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar override("YAC_MCP_NONEXIST_COMMAND", "x");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.issues.empty());
  REQUIRE(result.config.mcp.servers.empty());
}

TEST_CASE("override_mcp_result_max_bytes") {
  TempFile file("yac_test_mcp_result_env.toml");
  WriteFile(file.Path(),
            "[mcp]\n"
            "result_max_bytes = 1024\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar override("YAC_MCP_RESULT_MAX_BYTES", "4096");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.issues.empty());
  REQUIRE(result.config.mcp.result_max_bytes == 4096);
}

TEST_CASE("invalid_mcp_result_max_bytes_env_preserves_toml_value") {
  TempFile file("yac_test_mcp_result_env_invalid.toml");
  WriteFile(file.Path(),
            "[mcp]\n"
            "result_max_bytes = 1024\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");

  SECTION("non-numeric") {
    ScopedEnvVar override("YAC_MCP_RESULT_MAX_BYTES", "abc");

    const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
    REQUIRE(HasError(result, "YAC_MCP_RESULT_MAX_BYTES"));
    REQUIRE(result.config.mcp.result_max_bytes == 1024);
  }

  SECTION("zero") {
    ScopedEnvVar override("YAC_MCP_RESULT_MAX_BYTES", "0");

    const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
    REQUIRE(HasError(result, "YAC_MCP_RESULT_MAX_BYTES"));
    REQUIRE(result.config.mcp.result_max_bytes == 1024);
  }

  SECTION("too large") {
    ScopedEnvVar override("YAC_MCP_RESULT_MAX_BYTES", "184467440737095516160");

    const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
    REQUIRE(HasError(result, "YAC_MCP_RESULT_MAX_BYTES"));
    REQUIRE(result.config.mcp.result_max_bytes == 1024);
  }
}

TEST_CASE("empty_mcp_result_max_bytes_env_preserves_toml_value") {
  TempFile file("yac_test_mcp_result_env_empty.toml");
  WriteFile(file.Path(),
            "[mcp]\n"
            "result_max_bytes = 1024\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar override("YAC_MCP_RESULT_MAX_BYTES", "");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(HasError(result, "YAC_MCP_RESULT_MAX_BYTES"));
  REQUIRE(result.config.mcp.result_max_bytes == 1024);
}

TEST_CASE("mcp_args_env_override_uses_whitespace_splitting_not_commas") {
  TempFile file("yac_test_mcp_args_env_split.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"stdio\"\n"
            "command = \"npx\"\n"
            "args = [\"old\"]\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar override("YAC_MCP_CTX7_ARGS", "--one,--two --three");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.issues.empty());
  REQUIRE(result.config.mcp.servers.size() == 1);
  REQUIRE(result.config.mcp.servers[0].args ==
          std::vector<std::string>{"--one,--two", "--three"});
}

TEST_CASE("dynamic_mcp_env_overrides_cover_server_settings") {
  TempFile file("yac_test_mcp_env_all.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"stdio\"\n"
            "command = \"npx\"\n"
            "args = [\"old\"]\n"
            "enabled = false\n"
            "auto_start = true\n"
            "requires_approval = false\n"
            "approval_required_tools = [\"old_tool\"]\n"
            "[mcp.servers.env]\n"
            "OLD_ENV = \"old\"\n"
            "[mcp.servers.headers]\n"
            "X-Old = \"old\"\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar transport("YAC_MCP_CTX7_TRANSPORT", "http");
  ScopedEnvVar command("YAC_MCP_CTX7_COMMAND", "custom-bin");
  ScopedEnvVar args("YAC_MCP_CTX7_ARGS", "--flag value");
  ScopedEnvVar url("YAC_MCP_CTX7_URL", "https://example.test/mcp");
  ScopedEnvVar enabled("YAC_MCP_CTX7_ENABLED", "true");
  ScopedEnvVar auto_start("YAC_MCP_CTX7_AUTO_START", "false");
  ScopedEnvVar requires_approval("YAC_MCP_CTX7_REQUIRES_APPROVAL", "true");
  ScopedEnvVar approval_tools("YAC_MCP_CTX7_APPROVAL_REQUIRED_TOOLS",
                              "delete_repo run_command");
  ScopedEnvVar api_key_env("YAC_MCP_CTX7_API_KEY_ENV", "CTX7_TOKEN");
  ScopedEnvVar env_json("YAC_MCP_CTX7_ENV_JSON", R"({"A":"one","B":"two"})");
  ScopedEnvVar headers_json("YAC_MCP_CTX7_HEADERS_JSON",
                            R"({"X-Test":"yes","Accept":"json"})");
  ScopedEnvVar auth_type("YAC_MCP_CTX7_AUTH_TYPE", "oauth");
  ScopedEnvVar auth_url("YAC_MCP_CTX7_OAUTH_AUTHORIZATION_URL",
                        "https://auth.example/authorize");
  ScopedEnvVar token_url("YAC_MCP_CTX7_OAUTH_TOKEN_URL",
                         "https://auth.example/token");
  ScopedEnvVar client_id("YAC_MCP_CTX7_OAUTH_CLIENT_ID", "client-123");
  ScopedEnvVar scopes("YAC_MCP_CTX7_OAUTH_SCOPES", "read write");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.issues.empty());
  REQUIRE(result.config.mcp.servers.size() == 1);
  const auto& server = result.config.mcp.servers[0];
  REQUIRE(server.transport == "http");
  REQUIRE(server.command == "custom-bin");
  REQUIRE(server.args == std::vector<std::string>{"--flag", "value"});
  REQUIRE(server.url == "https://example.test/mcp");
  REQUIRE(server.enabled == true);
  REQUIRE(server.auto_start == false);
  REQUIRE(server.requires_approval == true);
  REQUIRE(server.approval_required_tools ==
          std::vector<std::string>{"delete_repo", "run_command"});
  REQUIRE(server.env == std::unordered_map<std::string, std::string>{
                            {"A", "one"}, {"B", "two"}});
  REQUIRE(server.headers == std::unordered_map<std::string, std::string>{
                                {"X-Test", "yes"}, {"Accept", "json"}});
  REQUIRE(BearerAuth(server) == nullptr);
  const auto* oauth = OAuthAuth(server);
  REQUIRE(oauth != nullptr);
  REQUIRE(oauth->authorization_url == "https://auth.example/authorize");
  REQUIRE(oauth->token_url == "https://auth.example/token");
  REQUIRE(oauth->client_id == "client-123");
  REQUIRE(oauth->scopes == std::vector<std::string>{"read", "write"});
}

TEST_CASE("dynamic_mcp_api_key_env_can_select_bearer_auth") {
  TempFile file("yac_test_mcp_env_bearer.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"http\"\n"
            "url = \"https://example.test/mcp\"\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar auth_type("YAC_MCP_CTX7_AUTH_TYPE", "bearer");
  ScopedEnvVar api_key_env("YAC_MCP_CTX7_API_KEY_ENV", "CTX7_TOKEN");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(result.issues.empty());
  REQUIRE(result.config.mcp.servers.size() == 1);
  const auto* bearer = BearerAuth(result.config.mcp.servers[0]);
  REQUIRE(bearer != nullptr);
  REQUIRE(bearer->api_key_env == "CTX7_TOKEN");
}

TEST_CASE("invalid_dynamic_mcp_json_override_is_rejected_atomically") {
  TempFile file("yac_test_mcp_env_bad_json.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"ctx7\"\n"
            "transport = \"stdio\"\n"
            "command = \"npx\"\n"
            "[mcp.servers.env]\n"
            "KEEP = \"toml\"\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar env_json("YAC_MCP_CTX7_ENV_JSON", R"({"A":1})");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(HasError(result, "YAC_MCP_CTX7_ENV_JSON"));
  REQUIRE(result.config.mcp.servers.size() == 1);
  REQUIRE(result.config.mcp.servers[0].env ==
          std::unordered_map<std::string, std::string>{{"KEEP", "toml"}});
}

TEST_CASE("colliding_upper_snake_mcp_server_ids_reject_dynamic_overrides") {
  TempFile file("yac_test_mcp_env_collision.toml");
  WriteFile(file.Path(),
            "[[mcp.servers]]\n"
            "id = \"foo-bar\"\n"
            "transport = \"stdio\"\n"
            "command = \"first\"\n"
            "\n"
            "[[mcp.servers]]\n"
            "id = \"foo_bar\"\n"
            "transport = \"stdio\"\n"
            "command = \"second\"\n");

  ScopedEnvVar api_key("OPENAI_API_KEY", "dummy-key");
  ScopedEnvVar override("YAC_MCP_FOO_BAR_COMMAND", "ambiguous");

  const auto result = yac::chat::LoadChatConfigResultFrom(file.Path(), false);
  REQUIRE(HasError(result, "upper-snake"));
  REQUIRE(result.config.mcp.servers.size() == 2);
  REQUIRE(result.config.mcp.servers[0].command == "first");
  REQUIRE(result.config.mcp.servers[1].command == "second");
}
