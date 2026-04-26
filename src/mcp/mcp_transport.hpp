#pragma once

#include <chrono>
#include <functional>
#include <nlohmann/json.hpp>
#include <stop_token>
#include <string_view>

namespace yac::mcp {

using Json = nlohmann::json;

enum class TransportStatus { Stopped, Starting, Ready, Reconnecting, Failed };

class IMcpTransport {
 public:
  virtual ~IMcpTransport() = default;
  virtual void Start() = 0;
  virtual void Stop(std::stop_token stop) = 0;
  [[nodiscard]] virtual Json SendRequest(std::string_view method,
                                         const Json& params,
                                         std::chrono::milliseconds timeout,
                                         std::stop_token stop) = 0;
  virtual void SendNotification(std::string_view method,
                                const Json& params) = 0;
  using NotificationCallback =
      std::function<void(std::string_view method, const Json& params)>;
  virtual void SetNotificationCallback(NotificationCallback callback) = 0;
  [[nodiscard]] virtual TransportStatus Status() const = 0;
};

}  // namespace yac::mcp
