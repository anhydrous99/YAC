#pragma once

#include "chat/types.hpp"
#include "core_types/mcp_manager_interface.hpp"
#include "mcp/mcp_transport.hpp"
#include "mcp/oauth/flow.hpp"

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace yac::mcp {

class IMcpTransport;
class ITokenStore;
class McpDebugLog;
class McpServerSession;

class McpManager : public core_types::IMcpManager {
 public:
  using EmitEventFn = std::function<void(chat::ChatEvent)>;
  using EmitIssueFn = std::function<void(chat::ConfigIssue)>;
  using TransportFactory =
      std::function<std::unique_ptr<IMcpTransport>(const McpServerConfig&)>;
  using AuthenticateFn = std::function<oauth::OAuthTokens(
      const McpServerConfig&, const oauth::OAuthInteractionMode&,
      std::stop_token)>;

  struct Dependencies {
    TransportFactory transport_factory;
    AuthenticateFn authenticate_fn;
    std::shared_ptr<ITokenStore> keychain_token_store;
    std::shared_ptr<ITokenStore> file_token_store;
    EmitIssueFn emit_issue;
  };

  McpManager(McpConfig config, EmitEventFn emit_event);
  McpManager(McpConfig config, EmitEventFn emit_event, Dependencies deps);
  ~McpManager() override;

  McpManager(const McpManager&) = delete;
  McpManager& operator=(const McpManager&) = delete;
  McpManager(McpManager&&) = delete;
  McpManager& operator=(McpManager&&) = delete;

  void Start();
  void Stop();

  [[nodiscard]] core_types::McpToolCatalogSnapshot GetToolCatalogSnapshot()
      const override;
  core_types::ToolExecutionResult InvokeTool(std::string_view qualified_name,
                                             std::string_view arguments_json,
                                             std::stop_token stop) override;
  [[nodiscard]] std::vector<core_types::McpServerStatus>
  GetServerStatusSnapshot() const override;
  std::vector<core_types::McpResourceDescriptor> ListResources(
      std::string_view server_id, std::stop_token stop) override;
  core_types::McpResourceContent ReadResource(std::string_view server_id,
                                              std::string_view uri,
                                              std::stop_token stop) override;

  void Authenticate(std::string_view server_id,
                    const oauth::OAuthInteractionMode& mode,
                    std::stop_token stop = {});

 private:
  class ObservedTransport;

  struct SessionRecord;
  struct SessionHandle;

  [[nodiscard]] static Dependencies BuildDefaultDependencies();
  [[nodiscard]] core_types::McpToolCatalogSnapshot BuildSnapshot(
      const std::vector<SessionHandle>& sessions) const;
  [[nodiscard]] std::vector<SessionHandle> SessionHandlesSnapshot() const;
  [[nodiscard]] SessionHandle RequireSessionHandle(
      std::string_view server_id) const;
  [[nodiscard]] SessionRecord& RequireRecord(std::string_view server_id);
  [[nodiscard]] const SessionRecord& RequireRecord(
      std::string_view server_id) const;
  void EnsureSessionsCreated() const;
  void EmitStateChanges() const;
  void HandleNotification(std::string_view server_id, std::string_view method,
                          const Json& params) const;

  McpConfig config_;
  EmitEventFn emit_event_;
  Dependencies deps_;
  mutable std::mutex mutex_;
  mutable std::vector<SessionRecord> sessions_;
  mutable core_types::McpToolCatalogSnapshot latest_snapshot_;
  mutable std::atomic<uint64_t> next_revision_id_{1};
  bool started_ = false;
};

}  // namespace yac::mcp
