#pragma once

#include "mcp/mcp_server_config.hpp"
#include "mcp/mcp_transport.hpp"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>

namespace yac::mcp {

class StreamableHttpMcpTransport : public IMcpTransport {
 public:
  explicit StreamableHttpMcpTransport(McpServerConfig config);
  ~StreamableHttpMcpTransport() override;

  StreamableHttpMcpTransport(const StreamableHttpMcpTransport&) = delete;
  StreamableHttpMcpTransport& operator=(const StreamableHttpMcpTransport&) =
      delete;
  StreamableHttpMcpTransport(StreamableHttpMcpTransport&&) = delete;
  StreamableHttpMcpTransport& operator=(StreamableHttpMcpTransport&&) = delete;

  void Start() override;
  void Stop(std::stop_token stop) override;
  [[nodiscard]] Json SendRequest(std::string_view method, const Json& params,
                                 std::chrono::milliseconds timeout,
                                 std::stop_token stop) override;
  void SendNotification(std::string_view method, const Json& params) override;
  void SetNotificationCallback(NotificationCallback callback) override;
  [[nodiscard]] TransportStatus Status() const override;

 private:
  struct RequestState;
  struct HttpResponse;

  static std::size_t HeaderCallback(char* buffer, std::size_t size,
                                    std::size_t nitems, void* userdata);
  static std::size_t WriteCallback(char* ptr, std::size_t size,
                                   std::size_t nmemb, void* userdata);

  enum class SessionRetry { Allow, Deny };

  [[nodiscard]] Json PerformJsonRpcRequest(const Json& message,
                                           std::chrono::milliseconds timeout,
                                           std::stop_token stop,
                                           SessionRetry session_retry);
  [[nodiscard]] HttpResponse PerformHttpRequest(
      const Json& message, std::chrono::milliseconds timeout,
      std::stop_token stop);
  [[nodiscard]] std::string ResolveAuthorizationHeader() const;
  [[nodiscard]] std::optional<std::string> SessionId() const;
  void SetSessionId(std::optional<std::string> session_id);
  void DispatchNotification(std::string_view method, const Json& params) const;

  McpServerConfig config_;
  mutable std::mutex mutex_;
  NotificationCallback notification_callback_;
  TransportStatus status_ = TransportStatus::Stopped;
  std::string auth_error_message_;
  std::optional<std::string> session_id_;
  std::mutex request_mutex_;
  std::atomic<std::int64_t> next_request_id_{1};
};

}  // namespace yac::mcp
