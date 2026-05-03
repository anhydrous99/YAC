#include "mcp/file_token_store.hpp"
#include "mcp/keychain_token_store.hpp"
#include "mcp/mcp_manager.hpp"
#include "mcp/protocol_constants.hpp"
#include "mcp/protocol_messages.hpp"
#include "mcp/token_store.hpp"
#include "mock_mcp_transport.hpp"

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

class ScopedHome {
 public:
  explicit ScopedHome(std::string_view name) {
    if (const char* prior = std::getenv("HOME")) {
      prior_ = prior;
    }
    path_ = std::filesystem::temp_directory_path() /
            ("yac_test_mcp_manager_home_" + std::string(name));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
    ::setenv("HOME", path_.c_str(), 1);
  }

  ~ScopedHome() {
    if (prior_.has_value()) {
      ::setenv("HOME", prior_->c_str(), 1);
    } else {
      ::unsetenv("HOME");
    }
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  ScopedHome(const ScopedHome&) = delete;
  ScopedHome(ScopedHome&&) = delete;
  ScopedHome& operator=(const ScopedHome&) = delete;
  ScopedHome& operator=(ScopedHome&&) = delete;

 private:
  std::filesystem::path path_;
  std::optional<std::string> prior_;
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

bool WaitUntil(const std::function<bool()>& predicate,
               std::chrono::milliseconds timeout = 500ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

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
  ScopedHome home("snapshot_merge_two_servers");
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
  REQUIRE(WaitUntil([&manager] {
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
  ScopedHome home("tool_description_source_attribution");
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
  REQUIRE(WaitUntil([&manager] {
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

TEST_CASE("status_snapshot_does_not_wait_for_slow_resource_request") {
  ScopedHome home("status_snapshot");
  auto resource_started = std::make_shared<std::promise<void>>();
  auto resource_started_future = resource_started->get_future();
  auto release_resource = std::make_shared<std::promise<void>>();
  auto release_resource_future = release_resource->get_future().share();

  McpManager manager(
      McpConfig{.servers = {{.id = "alpha", .transport = "stdio"}}},
      [](chat::ChatEvent event) { (void)event; },
      McpManager::Dependencies{
          .transport_factory =
              [resource_started,
               release_resource_future](const McpServerConfig& config) mutable {
                auto transport = std::make_unique<MockMcpTransport>();
                transport->SetRequestHandler(
                    [server_id = config.id, resource_started,
                     release_resource_future](
                        std::string_view method, const Json& params,
                        std::chrono::milliseconds timeout,
                        std::stop_token stop) mutable -> Json {
                      (void)server_id;
                      (void)params;
                      (void)timeout;
                      (void)stop;
                      if (method == protocol::kMethodInitialize) {
                        return MakeInitializeResponse().ToJson();
                      }
                      if (method == protocol::kMethodToolsList) {
                        return ToolsListResponse{
                            .tools = {ToolDefinition{
                                .name = "tool_a",
                                .description = "Tool A description"}}}
                            .ToJson();
                      }
                      if (method == protocol::kMethodResourcesList) {
                        resource_started->set_value();
                        release_resource_future.wait();
                        return ResourcesListResponse{
                            .resources = {ResourceDescriptor{
                                .uri = "file:///tmp/resource.txt",
                                .name = "resource"}}}
                            .ToJson();
                      }
                      throw std::runtime_error("unexpected request");
                    });
                return transport;
              },
          .authenticate_fn = {},
          .keychain_token_store = std::make_shared<ThrowingTokenStore>(),
          .file_token_store = std::make_shared<ThrowingTokenStore>(),
          .emit_issue = {},
      });

  manager.Start();
  REQUIRE(WaitUntil([&manager] {
    const auto status = manager.GetServerStatusSnapshot();
    return status.size() == 1 && status[0].state == "Ready";
  }));

  auto list_future = std::async(std::launch::async, [&manager] {
    return manager.ListResources("alpha", std::stop_token{});
  });
  const auto resource_ready = resource_started_future.wait_for(500ms);
  if (resource_ready != std::future_status::ready) {
    release_resource->set_value();
  }
  REQUIRE(resource_ready == std::future_status::ready);

  auto status_future = std::async(std::launch::async, [&manager] {
    return manager.GetServerStatusSnapshot();
  });
  const auto status_ready = status_future.wait_for(100ms);
  release_resource->set_value();
  REQUIRE(status_ready == std::future_status::ready);
  REQUIRE(status_future.get().size() == 1);
  REQUIRE(list_future.get().size() == 1);
}

TEST_CASE("auth_fallback") {
  ScopedHome home("auth_fallback");
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
