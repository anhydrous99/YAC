#pragma once

#include "mcp/mcp_transport.hpp"

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace yac::mcp::test {

struct RecordedRequest {
  std::string method;
  Json params;
};

struct RecordedNotification {
  std::string method;
  Json params;
};

class MockMcpTransport : public IMcpTransport {
 public:
  using RequestHandler = std::function<Json(
      std::string_view method, const Json& params,
      std::chrono::milliseconds timeout, std::stop_token stop)>;

  void AddCannedResponse(std::string method, Json response);
  void SetRequestHandler(RequestHandler handler);
  void EmitNotification(std::string_view method, const Json& params);

  void Start() override;
  void Stop(std::stop_token stop) override;
  [[nodiscard]] Json SendRequest(std::string_view method, const Json& params,
                                 std::chrono::milliseconds timeout,
                                 std::stop_token stop) override;
  void SendNotification(std::string_view method, const Json& params) override;
  void SetNotificationCallback(NotificationCallback callback) override;
  [[nodiscard]] TransportStatus Status() const override;

  [[nodiscard]] const std::vector<RecordedRequest>& RecordedRequests() const;
  [[nodiscard]] const std::vector<RecordedNotification>& RecordedNotifications()
      const;

 private:
  std::map<std::string, Json, std::less<>> canned_responses_;
  std::vector<RecordedRequest> recorded_requests_;
  std::vector<RecordedNotification> recorded_notifications_;
  NotificationCallback notification_callback_;
  RequestHandler request_handler_;
  TransportStatus status_ = TransportStatus::Stopped;
};

}  // namespace yac::mcp::test
