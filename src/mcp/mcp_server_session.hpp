#pragma once

#include "mcp/mcp_server_config.hpp"
#include "mcp/mcp_transport.hpp"
#include "mcp/protocol_messages.hpp"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace yac::mcp {

class McpDebugLog;

enum class ServerState {
  Disconnected,
  Connecting,
  Initializing,
  Ready,
  Reconnecting,
  Failed,
  ShuttingDown,
};

class McpServerSession {
 public:
  explicit McpServerSession(McpServerConfig config, IMcpTransport* transport,
                            McpDebugLog* debug_log = nullptr);
  ~McpServerSession();

  McpServerSession(const McpServerSession&) = delete;
  McpServerSession& operator=(const McpServerSession&) = delete;
  McpServerSession(McpServerSession&&) = delete;
  McpServerSession& operator=(McpServerSession&&) = delete;

  void Start();
  void Stop();
  [[nodiscard]] ServerState State() const;
  [[nodiscard]] std::string LastError() const;
  [[nodiscard]] const std::vector<ToolDefinition>& Tools() const;
  [[nodiscard]] const std::vector<ResourceDescriptor>& Resources() const;
  [[nodiscard]] const ServerCapabilities& Capabilities() const;
  void RefreshIfDirty();
  void MarkToolsDirty();
  void MarkResourcesDirty();

 private:
  void Run(std::stop_token stop_token);
  void HandleNotification(std::string_view method, const Json& params);
  void SetState(ServerState state);
  void SetFailure(std::string message);
  [[nodiscard]] InitializeRequest BuildInitializeRequest() const;
  void PerformHandshake(std::stop_token stop_token);
  void ValidateProtocolVersion(const std::string& server_protocol_version);
  [[nodiscard]] std::vector<ToolDefinition> FetchTools(
      std::stop_token stop_token) const;
  [[nodiscard]] std::vector<ResourceDescriptor> FetchResources(
      std::stop_token stop_token) const;

  McpServerConfig config_;
  IMcpTransport* transport_ = nullptr;
  McpDebugLog* debug_log_ = nullptr;
  mutable std::mutex mutex_;
  ServerState state_ = ServerState::Disconnected;
  std::string last_error_;
  ServerCapabilities capabilities_;
  std::vector<ToolDefinition> tools_;
  std::vector<ResourceDescriptor> resources_;
  std::atomic<bool> tools_dirty_{false};
  std::atomic<bool> resources_dirty_{false};
  std::jthread worker_;
};

}  // namespace yac::mcp
