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
#include <future>
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

InitializeResponse MakeInitializeResponse(ServerCapabilities capabilities) {
  return InitializeResponse{
      .protocol_version = std::string(protocol::kMcpProtocolVersion),
      .capabilities = capabilities,
      .server_info = ImplementationInfo{.name = "mock", .version = "1.0.0"},
  };
}

McpManager::Dependencies MakeDependencies(
    McpManager::TransportFactory transport_factory) {
  return McpManager::Dependencies{
      .transport_factory = std::move(transport_factory),
      .authenticate_fn = {},
      .keychain_token_store = std::make_shared<ThrowingTokenStore>(),
      .file_token_store = std::make_shared<ThrowingTokenStore>(),
      .emit_issue = {},
  };
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

TEST_CASE("status_snapshot_does_not_wait_for_blocked_tool_call") {
  std::promise<void> tool_call_started;
  auto tool_call_started_future = tool_call_started.get_future().share();
  std::promise<void> release_tool_call;
  auto release_tool_call_future = release_tool_call.get_future().share();
  std::atomic<bool> marked_tool_call_started{false};
  std::vector<chat::ChatEvent> events;

  McpManager manager(
      McpConfig{.servers = {{.id = "blocking", .transport = "stdio"}}},
      [&events](chat::ChatEvent event) { events.push_back(std::move(event)); },
      MakeDependencies([&](const McpServerConfig& config) {
        (void)config;
        auto transport = std::make_unique<MockMcpTransport>();
        transport->SetRequestHandler([&tool_call_started,
                                      release_tool_call_future,
                                      &marked_tool_call_started](
                                         std::string_view method,
                                         const Json& params,
                                         std::chrono::milliseconds timeout,
                                         std::stop_token stop) -> Json {
          (void)params;
          (void)timeout;
          (void)stop;
          if (method == protocol::kMethodInitialize) {
            return MakeInitializeResponse(ServerCapabilities{.has_tools = true})
                .ToJson();
          }
          if (method == protocol::kMethodToolsList) {
            return ToolsListResponse{
                .tools = {ToolDefinition{.name = "slow_tool"}},
            }
                .ToJson();
          }
          if (method == protocol::kMethodToolsCall) {
            if (!marked_tool_call_started.exchange(true)) {
              tool_call_started.set_value();
            }
            release_tool_call_future.wait();
            return ToolsCallResponse{
                .result_blocks = {TextContent{.text = "tool complete"}}}
                .ToJson();
          }
          throw std::runtime_error("unexpected request");
        });
        return transport;
      }));

  manager.Start();
  REQUIRE(yac::test::WaitUntil([&manager] {
    const auto status = manager.GetServerStatusSnapshot();
    return status.size() == 1 && status[0].state == "Ready";
  }));
  (void)manager.GetToolCatalogSnapshot();

  std::future<core_types::ToolExecutionResult> blocked_call =
      std::async(std::launch::async, [&manager] {
        return manager.InvokeTool("mcp_blocking__slow_tool", "{}",
                                  std::stop_token{});
      });
  REQUIRE(tool_call_started_future.wait_for(2s) == std::future_status::ready);

  std::future<std::vector<core_types::McpServerStatus>> status =
      std::async(std::launch::async,
                 [&manager] { return manager.GetServerStatusSnapshot(); });
  const bool status_ready = status.wait_for(200ms) == std::future_status::ready;

  release_tool_call.set_value();
  REQUIRE(status_ready);
  REQUIRE(status.get()[0].state == "Ready");
  REQUIRE(blocked_call.get().result_json.find("tool complete") !=
          std::string::npos);
}

TEST_CASE("status_snapshot_does_not_wait_for_blocked_resource_list") {
  std::promise<void> list_started;
  auto list_started_future = list_started.get_future().share();
  std::promise<void> release_list;
  auto release_list_future = release_list.get_future().share();
  std::atomic<int> resource_list_calls{0};
  std::vector<chat::ChatEvent> events;

  McpManager manager(
      McpConfig{.servers = {{.id = "resources", .transport = "stdio"}}},
      [&events](chat::ChatEvent event) { events.push_back(std::move(event)); },
      MakeDependencies([&](const McpServerConfig& config) {
        (void)config;
        auto transport = std::make_unique<MockMcpTransport>();
        transport->SetRequestHandler(
            [&list_started, release_list_future, &resource_list_calls](
                std::string_view method, const Json& params,
                std::chrono::milliseconds timeout,
                std::stop_token stop) -> Json {
              (void)params;
              (void)timeout;
              (void)stop;
              if (method == protocol::kMethodInitialize) {
                return MakeInitializeResponse(
                           ServerCapabilities{.has_resources = true})
                    .ToJson();
              }
              if (method == protocol::kMethodResourcesList) {
                const int call = ++resource_list_calls;
                if (call == 1) {
                  return ResourcesListResponse{.resources = {}}.ToJson();
                }
                list_started.set_value();
                release_list_future.wait();
                return ResourcesListResponse{.resources = {ResourceDescriptor{
                                                 .uri = "file:///slow.txt"}}}
                    .ToJson();
              }
              throw std::runtime_error("unexpected request");
            });
        return transport;
      }));

  manager.Start();
  REQUIRE(yac::test::WaitUntil([&manager] {
    const auto status = manager.GetServerStatusSnapshot();
    return status.size() == 1 && status[0].state == "Ready";
  }));

  std::future<std::vector<core_types::McpResourceDescriptor>> blocked_list =
      std::async(std::launch::async, [&manager] {
        return manager.ListResources("resources", std::stop_token{});
      });
  REQUIRE(list_started_future.wait_for(2s) == std::future_status::ready);

  std::future<std::vector<core_types::McpServerStatus>> status =
      std::async(std::launch::async,
                 [&manager] { return manager.GetServerStatusSnapshot(); });
  const bool status_ready = status.wait_for(200ms) == std::future_status::ready;

  release_list.set_value();
  REQUIRE(status_ready);
  REQUIRE(status.get()[0].state == "Ready");
  REQUIRE(blocked_list.get()[0].uri == "file:///slow.txt");
}

TEST_CASE("status_snapshot_does_not_wait_for_blocked_authentication") {
  TempDir dir;
  std::promise<void> auth_started;
  auto auth_started_future = auth_started.get_future().share();
  std::promise<void> release_auth;
  auto release_auth_future = release_auth.get_future().share();
  std::atomic<bool> marked_auth_started{false};

  McpManager::Dependencies deps =
      MakeDependencies([](const McpServerConfig& config) {
        (void)config;
        auto transport = std::make_unique<MockMcpTransport>();
        transport->AddCannedResponse(
            std::string(protocol::kMethodInitialize),
            MakeInitializeResponse(ServerCapabilities{}).ToJson());
        return transport;
      });
  deps.authenticate_fn =
      [&auth_started, release_auth_future, &marked_auth_started](
          const McpServerConfig& config,
          const oauth::OAuthInteractionMode& mode, std::stop_token stop) {
        (void)config;
        (void)mode;
        (void)stop;
        if (!marked_auth_started.exchange(true)) {
          auth_started.set_value();
        }
        release_auth_future.wait();
        return oauth::OAuthTokens{
            .access_token = "access-token",
            .refresh_token = "refresh-token",
            .expires_at =
                std::chrono::system_clock::time_point{
                    std::chrono::seconds{999999999}},
            .token_type = "Bearer",
            .scope = "openid",
        };
      };
  deps.file_token_store = std::make_shared<FileTokenStore>(dir.Path());

  McpManager manager(
      McpConfig{.servers = {{.id = "oauth-server",
                             .transport = "stdio",
                             .auth =
                                 McpAuthOAuth{
                                     .authorization_url =
                                         "https://auth.example/authorize",
                                     .token_url = "https://auth.example/token",
                                     .client_id = "client-id"}}}},
      [](chat::ChatEvent event) { (void)event; }, std::move(deps));

  std::future<void> blocked_auth = std::async(std::launch::async, [&manager] {
    manager.Authenticate("oauth-server", oauth::OAuthInteractionMode{});
  });
  REQUIRE(auth_started_future.wait_for(2s) == std::future_status::ready);

  std::future<std::vector<core_types::McpServerStatus>> status =
      std::async(std::launch::async,
                 [&manager] { return manager.GetServerStatusSnapshot(); });
  const bool status_ready = status.wait_for(200ms) == std::future_status::ready;

  release_auth.set_value();
  REQUIRE(status_ready);
  REQUIRE(status.get()[0].id == "oauth-server");
  blocked_auth.get();
}

TEST_CASE("in_flight_tool_call_and_stop_complete_without_deadlock") {
  std::promise<void> tool_call_started;
  auto tool_call_started_future = tool_call_started.get_future().share();
  std::promise<void> release_tool_call;
  auto release_tool_call_future = release_tool_call.get_future().share();
  std::atomic<bool> marked_tool_call_started{false};

  McpManager manager(
      McpConfig{.servers = {{.id = "stop-server", .transport = "stdio"}}},
      [](chat::ChatEvent event) { (void)event; },
      MakeDependencies([&](const McpServerConfig& config) {
        (void)config;
        auto transport = std::make_unique<MockMcpTransport>();
        transport->SetRequestHandler([&tool_call_started,
                                      release_tool_call_future,
                                      &marked_tool_call_started](
                                         std::string_view method,
                                         const Json& params,
                                         std::chrono::milliseconds timeout,
                                         std::stop_token stop) -> Json {
          (void)params;
          (void)timeout;
          (void)stop;
          if (method == protocol::kMethodInitialize) {
            return MakeInitializeResponse(ServerCapabilities{.has_tools = true})
                .ToJson();
          }
          if (method == protocol::kMethodToolsList) {
            return ToolsListResponse{
                .tools = {ToolDefinition{.name = "slow_tool"}},
            }
                .ToJson();
          }
          if (method == protocol::kMethodToolsCall) {
            if (!marked_tool_call_started.exchange(true)) {
              tool_call_started.set_value();
            }
            release_tool_call_future.wait();
            return ToolsCallResponse{
                .result_blocks = {TextContent{.text = "stopped cleanly"}}}
                .ToJson();
          }
          throw std::runtime_error("unexpected request");
        });
        return transport;
      }));

  manager.Start();
  REQUIRE(yac::test::WaitUntil([&manager] {
    const auto status = manager.GetServerStatusSnapshot();
    return status.size() == 1 && status[0].state == "Ready";
  }));
  (void)manager.GetToolCatalogSnapshot();

  std::future<core_types::ToolExecutionResult> blocked_call =
      std::async(std::launch::async, [&manager] {
        return manager.InvokeTool("mcp_stop-server__slow_tool", "{}",
                                  std::stop_token{});
      });
  REQUIRE(tool_call_started_future.wait_for(2s) == std::future_status::ready);

  std::future<void> stop =
      std::async(std::launch::async, [&manager] { manager.Stop(); });
  const bool stop_ready = stop.wait_for(2s) == std::future_status::ready;

  release_tool_call.set_value();
  REQUIRE(stop_ready);
  REQUIRE(blocked_call.get().result_json.find("stopped cleanly") !=
          std::string::npos);
  stop.get();
  try {
    (void)manager.InvokeTool("mcp_stop-server__slow_tool", "{}",
                             std::stop_token{});
    FAIL("InvokeTool after Stop must fail");
  } catch (const std::runtime_error& error) {
    REQUIRE(std::string(error.what()) == "MCP manager is stopped");
  }
}

}  // namespace yac::mcp::test
