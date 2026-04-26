#include "mock_mcp_transport.hpp"

#include <stdexcept>
#include <string>
#include <utility>

namespace yac::mcp::test {

void MockMcpTransport::AddCannedResponse(std::string method, Json response) {
  canned_responses_.emplace(std::move(method), std::move(response));
}

void MockMcpTransport::SetRequestHandler(RequestHandler handler) {
  request_handler_ = std::move(handler);
}

void MockMcpTransport::EmitNotification(std::string_view method,
                                        const Json& params) {
  if (notification_callback_) {
    notification_callback_(method, params);
  }
}

void MockMcpTransport::Start() {
  status_ = TransportStatus::Ready;
}

void MockMcpTransport::Stop(std::stop_token stop) {
  (void)stop;
  status_ = TransportStatus::Stopped;
}

Json MockMcpTransport::SendRequest(std::string_view method, const Json& params,
                                   std::chrono::milliseconds timeout,
                                   std::stop_token stop) {
  (void)timeout;
  (void)stop;
  recorded_requests_.push_back({std::string(method), params});
  if (request_handler_) {
    return request_handler_(method, params, timeout, stop);
  }
  auto it = canned_responses_.find(method);
  if (it == canned_responses_.end()) {
    throw std::runtime_error("No canned response for method: " +
                             std::string(method));
  }
  return it->second;
}

void MockMcpTransport::SendNotification(std::string_view method,
                                        const Json& params) {
  recorded_notifications_.push_back({std::string(method), params});
}

void MockMcpTransport::SetNotificationCallback(NotificationCallback callback) {
  notification_callback_ = std::move(callback);
}

TransportStatus MockMcpTransport::Status() const {
  return status_;
}

const std::vector<RecordedRequest>& MockMcpTransport::RecordedRequests() const {
  return recorded_requests_;
}

const std::vector<RecordedNotification>&
MockMcpTransport::RecordedNotifications() const {
  return recorded_notifications_;
}

}  // namespace yac::mcp::test
