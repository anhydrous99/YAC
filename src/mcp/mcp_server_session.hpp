#pragma once

#include "mcp/mcp_server_config.hpp"
#include "mcp/mcp_transport.hpp"
#include "mcp/protocol_constants.hpp"
#include "mcp/protocol_messages.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
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
                            McpDebugLog* debug_log = nullptr,
                            std::chrono::milliseconds initial_reconnect_delay =
                                protocol::kReconnectInitialDelayMs);
  ~McpServerSession();

  McpServerSession(const McpServerSession&) = delete;
  McpServerSession& operator=(const McpServerSession&) = delete;
  McpServerSession(McpServerSession&&) = delete;
  McpServerSession& operator=(McpServerSession&&) = delete;

  void Start();
  void Stop();
  [[nodiscard]] ServerState State() const;
  [[nodiscard]] std::string LastError() const;
  [[nodiscard]] std::shared_ptr<const std::vector<ToolDefinition>> Tools()
      const;
  [[nodiscard]] std::shared_ptr<const std::vector<ResourceDescriptor>>
  Resources() const;
  [[nodiscard]] const ServerCapabilities& Capabilities() const;
  void RefreshIfDirty();
  void MarkToolsDirty();
  void MarkResourcesDirty();

 private:
  void Run(std::stop_token stop_token);
  void BackoffSleep(std::stop_token stop_token,
                    std::chrono::milliseconds delay);
  void HandleNotification(std::string_view method, const Json& params);
  void SetState(ServerState state);
  void SetFailure(std::string message);
  [[nodiscard]] static InitializeRequest BuildInitializeRequest();
  void PerformHandshake(std::stop_token stop_token);
  static void ValidateProtocolVersion(
      const std::string& server_protocol_version);
  [[nodiscard]] std::vector<ToolDefinition> FetchTools(
      std::stop_token stop_token);
  [[nodiscard]] std::vector<ResourceDescriptor> FetchResources(
      std::stop_token stop_token);
  [[nodiscard]] Json SendTracked(std::string_view method, const Json& params,
                                 std::chrono::milliseconds timeout,
                                 std::stop_token stop_token);
  void PurgeStaleCancelledIds();

  McpServerConfig config_;
  IMcpTransport* transport_ = nullptr;
  McpDebugLog* debug_log_ = nullptr;
  std::chrono::milliseconds initial_reconnect_delay_;
  mutable std::mutex mutex_;
  std::mutex reconnect_mutex_;
  std::condition_variable_any reconnect_cv_;
  std::mt19937 rng_;
  ServerState state_ = ServerState::Disconnected;
  std::string last_error_;
  ServerCapabilities capabilities_;
  std::shared_ptr<const std::vector<ToolDefinition>> tools_;
  std::shared_ptr<const std::vector<ResourceDescriptor>> resources_;
  std::atomic<bool> tools_dirty_{false};
  std::atomic<bool> resources_dirty_{false};
  std::atomic<std::int64_t> next_request_id_{0};
  std::atomic<std::int64_t> inflight_request_id_{-1};
  std::mutex cancelled_mutex_;
  std::unordered_map<std::int64_t, std::chrono::steady_clock::time_point>
      cancelled_ids_;
  std::jthread worker_;
};

}  // namespace yac::mcp
