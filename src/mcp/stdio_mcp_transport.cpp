#include "mcp/stdio_mcp_transport.hpp"

#include "mcp/protocol_constants.hpp"
#include "tool_call/json_rpc_stdio_base.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <vector>

namespace yac::mcp {
namespace {

namespace pc = protocol;

constexpr auto kStopGracePeriod = std::chrono::seconds(3);
constexpr std::size_t kMaxFrameBytes = 64UL * 1024UL * 1024UL;
constexpr auto kCancellationFlushDelay = std::chrono::milliseconds(100);

[[nodiscard]] std::pair<std::string, std::vector<std::string>> BuildCommand(
    const McpServerConfig& config) {
  if (config.command.empty()) {
    throw std::runtime_error("MCP stdio transport requires a command");
  }

  if (config.env.empty()) {
    return {config.command, config.args};
  }

  std::vector<std::string> args;
  args.reserve(config.env.size() + config.args.size() + 1);
  for (const auto& [key, value] : config.env) {
    args.push_back(key + "=" + value);
  }
  args.push_back(config.command);
  args.insert(args.end(), config.args.begin(), config.args.end());
  return {"/usr/bin/env", std::move(args)};
}

}  // namespace

class StdioMcpTransport::Client final : public tool_call::JsonRpcStdioBase {
 public:
  explicit Client(StdioMcpTransport* owner)
      : tool_call::JsonRpcStdioBase("MCP stdio"), owner_(owner) {}

  void StartProcess(const std::string& command,
                    const std::vector<std::string>& args) {
    Start(command, args);
  }

  void StopProcess() { Stop(); }

  [[nodiscard]] Json SendRequestMessage(std::string_view method,
                                        const Json& params,
                                        std::chrono::milliseconds timeout) {
    return SendRequest(method, params, timeout);
  }

  void SendNotificationMessage(std::string_view method, const Json& params) {
    SendNotification(method, params);
  }

 protected:
  void WriteFrame(const std::string& body) override {
    if (body.find('\n') != std::string::npos ||
        body.find('\r') != std::string::npos) {
      throw std::runtime_error("MCP stdio frames must not contain newlines");
    }
    WriteBytes(body);
    WriteBytes("\n");
  }

  std::optional<std::string> ReadFrame() override {
    std::string line;
    line.reserve(256);

    const auto fail =
        [this](std::string message) -> std::optional<std::string> {
      FaultAllPending(std::move(message));
      return std::nullopt;
    };

    std::array<char, 1> byte{};
    while (true) {
      const auto result = read(ReadFd(), byte.data(), byte.size());
      if (result <= 0) {
        return std::nullopt;
      }

      if (byte[0] == '\n') {
        break;
      }

      if (byte[0] == '\r') {
        return fail(
            "Invalid MCP stdio JSON frame: carriage return not allowed");
      }

      line.push_back(byte[0]);
      if (line.size() > kMaxFrameBytes) {
        return fail("Invalid MCP stdio JSON frame: frame exceeded max size");
      }
    }

    if (line.empty()) {
      return fail("Invalid MCP stdio JSON frame: frame must not be empty");
    }

    try {
      const auto parsed = Json::parse(line);
      (void)parsed;
    } catch (const std::exception& error) {
      return fail(std::string("Invalid MCP stdio JSON frame: ") + error.what());
    }
    return line;
  }

  void OnNotification(std::string_view method, const Json& params) override {
    owner_->HandleNotification(method, params);
  }

 private:
  StdioMcpTransport* owner_;
};

StdioMcpTransport::StdioMcpTransport(McpServerConfig config)
    : config_(std::move(config)), client_(std::make_unique<Client>(this)) {}

StdioMcpTransport::~StdioMcpTransport() {
  Stop(std::stop_token{});
}

void StdioMcpTransport::Start() {
  std::lock_guard lock(mutex_);
  if (status_ == TransportStatus::Ready ||
      status_ == TransportStatus::Starting) {
    return;
  }

  status_ = TransportStatus::Starting;
  try {
    auto [command, args] = BuildCommand(config_);
    client_->StartProcess(command, args);
    status_ = TransportStatus::Ready;
  } catch (...) {
    status_ = TransportStatus::Failed;
    throw;
  }
}

void StdioMcpTransport::Stop(std::stop_token stop) {
  std::vector<std::int64_t> inflight_ids;
  {
    std::lock_guard lock(request_mutex_);
    inflight_ids.assign(inflight_request_ids_.begin(),
                        inflight_request_ids_.end());
  }

  for (const auto request_id : inflight_ids) {
    if (stop.stop_requested()) {
      break;
    }
    SendCancelledNotification(request_id, "transport stopped");
  }
  if (!inflight_ids.empty() && !stop.stop_requested()) {
    std::this_thread::sleep_for(kCancellationFlushDelay);
  }

  std::lock_guard lock(mutex_);
  if (status_ == TransportStatus::Stopped) {
    return;
  }

  try {
    client_->StopProcess();
    (void)kStopGracePeriod;
    status_ = TransportStatus::Stopped;
  } catch (...) {
    status_ = TransportStatus::Failed;
    throw;
  }
}

Json StdioMcpTransport::SendRequest(std::string_view method, const Json& params,
                                    std::chrono::milliseconds timeout,
                                    std::stop_token stop) {
  const std::int64_t request_id = next_request_id_.fetch_add(1);
  {
    std::lock_guard lock(request_mutex_);
    inflight_request_ids_.insert(request_id);
    cancelled_request_ids_.erase(request_id);
  }

  std::stop_callback cancelled_callback(stop, [this, request_id] {
    try {
      SendCancelledNotification(request_id, "request cancelled");
    } catch (const std::exception&) {
    }
  });

  try {
    auto response = client_->SendRequestMessage(method, params, timeout);
    {
      std::lock_guard lock(request_mutex_);
      inflight_request_ids_.erase(request_id);
      cancelled_request_ids_.erase(request_id);
    }
    return response;
  } catch (...) {
    {
      std::lock_guard lock(mutex_);
      if (status_ != TransportStatus::Stopped) {
        status_ = TransportStatus::Failed;
      }
    }
    {
      std::lock_guard lock(request_mutex_);
      inflight_request_ids_.erase(request_id);
      cancelled_request_ids_.erase(request_id);
    }
    throw;
  }
}

void StdioMcpTransport::SendNotification(std::string_view method,
                                         const Json& params) {
  client_->SendNotificationMessage(method, params);
}

void StdioMcpTransport::SetNotificationCallback(NotificationCallback callback) {
  std::lock_guard lock(mutex_);
  notification_callback_ = std::move(callback);
}

TransportStatus StdioMcpTransport::Status() const {
  std::lock_guard lock(mutex_);
  return status_;
}

void StdioMcpTransport::HandleNotification(std::string_view method,
                                           const Json& params) {
  NotificationCallback callback;
  {
    std::lock_guard lock(mutex_);
    callback = notification_callback_;
  }
  if (callback) {
    callback(method, params);
  }
}

void StdioMcpTransport::SendCancelledNotification(std::int64_t request_id,
                                                  std::string_view reason) {
  {
    std::lock_guard lock(request_mutex_);
    if (!inflight_request_ids_.contains(request_id) ||
        cancelled_request_ids_.contains(request_id)) {
      return;
    }
    cancelled_request_ids_.insert(request_id);
  }

  Json params = Json::object();
  params[std::string(pc::kFieldRequestId)] = request_id;
  params[std::string(pc::kFieldReason)] = reason;
  client_->SendNotificationMessage(pc::kMethodNotificationsCancelled, params);
}

}  // namespace yac::mcp
