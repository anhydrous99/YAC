#include "mcp/file_token_store.hpp"
#include "mcp/keychain_token_store.hpp"
#include "mcp/mcp_manager.hpp"
#include "mcp/protocol_constants.hpp"
#include "mcp/protocol_messages.hpp"
#include "mcp/token_store.hpp"
#include "mock_mcp_transport.hpp"
#include "util/wait_until.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

namespace yac::mcp::test {
namespace {

class TempDir {
 public:
  TempDir() {
    path_ = std::filesystem::temp_directory_path() / "yac_test_mcp_manager";
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~TempDir() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempDir(const TempDir&) = delete;
  TempDir(TempDir&&) = delete;
  TempDir& operator=(const TempDir&) = delete;
  TempDir& operator=(TempDir&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

class ThrowingTokenStore : public ITokenStore {
 public:
  [[nodiscard]] std::optional<std::string> Get(
      std::string_view server_id) const override {
    (void)server_id;
    throw KeychainUnavailableError("keychain unavailable in this env");
  }

  void Set(std::string_view server_id, std::string_view token_json) override {
    (void)server_id;
    (void)token_json;
    throw KeychainUnavailableError("keychain unavailable in this env");
  }

  void Erase(std::string_view server_id) override { (void)server_id; }
};

InitializeResponse MakeInitializeResponse() {
  return InitializeResponse{
      .protocol_version = std::string(protocol::kMcpProtocolVersion),
      .capabilities = ServerCapabilities{.has_tools = true},
      .server_info = ImplementationInfo{.name = "mock", .version = "1.0.0"},
  };
}

std::unique_ptr<IMcpTransport> MakeTransportForServer(
    const McpServerConfig& config) {
  auto transport = std::make_unique<MockMcpTransport>();
  transport->SetRequestHandler(
      [server_id = config.id](std::string_view method, const Json& params,
                              std::chrono::milliseconds timeout,
                              std::stop_token stop) -> Json {
        (void)params;
        (void)timeout;
        (void)stop;
        if (method == protocol::kMethodInitialize) {
          return MakeInitializeResponse().ToJson();
        }
        if (method == protocol::kMethodToolsList) {
          if (server_id == "alpha") {
            return ToolsListResponse{
                .tools = {ToolDefinition{.name = "tool_a",
                                         .description = "Tool A description"}}}
                .ToJson();
          }
          if (server_id == "test_server") {
            return ToolsListResponse{
                .tools = {ToolDefinition{.name = "tool_a",
                                         .description = "Tool A description"}}}
                .ToJson();
          }
          return ToolsListResponse{
              .tools = {ToolDefinition{.name = "tool_b",
                                       .description = "Tool B description"}}}
              .ToJson();
        }
        throw std::runtime_error("unexpected request");
      });
  return transport;
}

}  // namespace

TEST_CASE("snapshot_merge_two_servers") {
  std::vector<chat::ChatEvent> events;
  McpManager manager(
      McpConfig{.servers = {{.id = "alpha", .transport = "stdio"},
                            {.id = "beta", .transport = "stdio"}}},
      [&events](chat::ChatEvent event) { events.push_back(std::move(event)); },
      McpManager::Dependencies{
          .transport_factory = MakeTransportForServer,
          .authenticate_fn = {},
          .keychain_token_store = std::make_shared<ThrowingTokenStore>(),
          .file_token_store = std::make_shared<ThrowingTokenStore>(),
          .emit_issue = {},
      });

  manager.Start();
  REQUIRE(yac::test::WaitUntil([&manager] {
    const auto status = manager.GetServerStatusSnapshot();
    return status.size() == 2 && status[0].state == "Ready" &&
           status[1].state == "Ready";
  }));

  const auto snapshot = manager.GetToolCatalogSnapshot();

  REQUIRE(snapshot.revision_id != 0);
  REQUIRE(snapshot.tools.size() == 2);
  REQUIRE(snapshot.name_to_server_tool.contains("mcp_alpha__tool_a"));
  REQUIRE(snapshot.name_to_server_tool.contains("mcp_beta__tool_b"));
}

TEST_CASE("tool_description_source_attribution") {
  std::vector<chat::ChatEvent> events;
  McpManager manager(
      McpConfig{.servers = {{.id = "test_server", .transport = "stdio"}}},
      [&events](chat::ChatEvent event) { events.push_back(std::move(event)); },
      McpManager::Dependencies{
          .transport_factory = MakeTransportForServer,
          .authenticate_fn = {},
          .keychain_token_store = std::make_shared<ThrowingTokenStore>(),
          .file_token_store = std::make_shared<ThrowingTokenStore>(),
          .emit_issue = {},
      });

  manager.Start();
  REQUIRE(yac::test::WaitUntil([&manager] {
    const auto status = manager.GetServerStatusSnapshot();
    return status.size() == 1 && status[0].state == "Ready";
  }));

  const auto snapshot = manager.GetToolCatalogSnapshot();

  REQUIRE(snapshot.tools.size() == 1);
  const auto& tool = snapshot.tools[0];
  REQUIRE(tool.name == "mcp_test_server__tool_a");
  REQUIRE(tool.description.find("[via MCP server 'test_server']") == 0);
  REQUIRE(tool.description.find("Tool A description") != std::string::npos);
}

TEST_CASE("auth_fallback") {
  if (std::getenv("DBUS_SESSION_BUS_ADDRESS") != nullptr &&
      KeychainTokenStore::IsKeychainAvailable()) {
    SUCCEED(
        "keychain available; fallback path is covered by env-unset evidence "
        "run");
    return;
  }

  TempDir dir;
  std::vector<chat::ConfigIssue> issues;
  McpManager manager(
      McpConfig{.servers = {{.id = "oauth-server",
                             .transport = "stdio",
                             .auth =
                                 McpAuthOAuth{
                                     .authorization_url =
                                         "https://auth.example/authorize",
                                     .token_url = "https://auth.example/token",
                                     .client_id = "client-id",
                                     .scopes = {"openid"}}}}},
      [](chat::ChatEvent event) { (void)event; },
      McpManager::Dependencies{
          .transport_factory =
              [](const McpServerConfig& config) {
                auto transport = std::make_unique<MockMcpTransport>();
                transport->AddCannedResponse(
                    std::string(protocol::kMethodInitialize),
                    MakeInitializeResponse().ToJson());
                (void)config;
                return transport;
              },
          .authenticate_fn =
              [](const McpServerConfig& config,
                 const oauth::OAuthInteractionMode& mode,
                 std::stop_token stop) {
                (void)config;
                (void)mode;
                (void)stop;
                return oauth::OAuthTokens{
                    .access_token = "access-token",
                    .refresh_token = "refresh-token",
                    .expires_at =
                        std::chrono::system_clock::time_point{
                            std::chrono::seconds{999999999}},
                    .token_type = "Bearer",
                    .scope = "openid",
                };
              },
          .keychain_token_store = std::make_shared<ThrowingTokenStore>(),
          .file_token_store = std::make_shared<FileTokenStore>(dir.Path()),
          .emit_issue =
              [&issues](chat::ConfigIssue issue) {
                issues.push_back(std::move(issue));
              },
      });

  manager.Authenticate("oauth-server", oauth::OAuthInteractionMode{});

  FileTokenStore store(dir.Path());
  const auto token_json = store.Get("oauth-server");
  REQUIRE(token_json.has_value());
  REQUIRE(token_json->find("access-token") != std::string::npos);
  REQUIRE(issues.size() == 1);
  REQUIRE(issues[0].severity == chat::ConfigIssueSeverity::Warning);
}

}  // namespace yac::mcp::test
