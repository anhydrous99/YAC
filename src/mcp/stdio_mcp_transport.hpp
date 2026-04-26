#pragma once

#include "mcp/mcp_server_config.hpp"
#include "mcp/mcp_transport.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <set>
#include <stop_token>

namespace yac::mcp {

class StdioMcpTransport : public IMcpTransport {
 public:
  explicit StdioMcpTransport(McpServerConfig config);
  ~StdioMcpTransport() override;

  StdioMcpTransport(const StdioMcpTransport&) = delete;
  StdioMcpTransport& operator=(const StdioMcpTransport&) = delete;
  StdioMcpTransport(StdioMcpTransport&&) = delete;
  StdioMcpTransport& operator=(StdioMcpTransport&&) = delete;

  void Start() override;
  void Stop(std::stop_token stop) override;
  [[nodiscard]] Json SendRequest(std::string_view method, const Json& params,
                                 std::chrono::milliseconds timeout,
                                 std::stop_token stop) override;
  void SendNotification(std::string_view method, const Json& params) override;
  void SetNotificationCallback(NotificationCallback callback) override;
  [[nodiscard]] TransportStatus Status() const override;

 private:
  class Client;

  void HandleNotification(std::string_view method, const Json& params);
  void SendCancelledNotification(std::int64_t request_id,
                                 std::string_view reason);

  McpServerConfig config_;
  std::unique_ptr<Client> client_;
  mutable std::mutex mutex_;
  NotificationCallback notification_callback_;
  TransportStatus status_ = TransportStatus::Stopped;
  std::atomic<std::int64_t> next_request_id_{1};
  mutable std::mutex request_mutex_;
  std::set<std::int64_t> inflight_request_ids_;
  std::set<std::int64_t> cancelled_request_ids_;
};

}  // namespace yac::mcp
