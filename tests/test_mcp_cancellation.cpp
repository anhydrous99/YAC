#include "mcp/mcp_server_session.hpp"
#include "mcp/protocol_constants.hpp"
#include "mock_mcp_transport.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

namespace yac::mcp::test {
namespace {

namespace pc = yac::mcp::protocol;

template <typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout = 500ms) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(5ms);
  }
  return predicate();
}

McpServerConfig MakeConfig() {
  return McpServerConfig{.id = "mock-server", .transport = "stdio"};
}

}  // namespace

TEST_CASE("emits_notification_on_cancel") {
  MockMcpTransport transport;
  std::atomic<bool> request_received{false};

  transport.SetRequestHandler([&](std::string_view method, const Json&,
                                  std::chrono::milliseconds,
                                  std::stop_token stop) -> Json {
    if (method == std::string(pc::kMethodInitialize)) {
      request_received = true;
      while (!stop.stop_requested()) {
        std::this_thread::sleep_for(5ms);
      }
      throw std::runtime_error("cancelled");
    }
    throw std::runtime_error("unexpected method");
  });

  McpServerSession session(MakeConfig(), &transport);
  session.Start();

  REQUIRE(WaitUntil([&] { return request_received.load(); }, 2s));
  session.Stop();

  const auto& notifications = transport.RecordedNotifications();
  const auto cancel_it =
      std::ranges::find_if(notifications, [](const RecordedNotification& n) {
        return n.method == std::string(pc::kMethodNotificationsCancelled);
      });
  REQUIRE(cancel_it != notifications.end());
  REQUIRE(cancel_it->params.contains(std::string(pc::kFieldRequestId)));
  REQUIRE(cancel_it->params.contains(std::string(pc::kFieldReason)));
  REQUIRE(cancel_it->params[std::string(pc::kFieldReason)] == "user cancelled");
}

TEST_CASE("ignores_late_response") {
  MockMcpTransport transport;
  std::atomic<bool> request_received{false};

  transport.SetRequestHandler([&](std::string_view method, const Json&,
                                  std::chrono::milliseconds,
                                  std::stop_token stop) -> Json {
    if (method == std::string(pc::kMethodInitialize)) {
      request_received = true;
      while (!stop.stop_requested()) {
        std::this_thread::sleep_for(5ms);
      }
      return InitializeResponse{
          .protocol_version = std::string(pc::kMcpProtocolVersion),
          .capabilities = ServerCapabilities{.has_tools = true},
          .server_info = ImplementationInfo{.name = "mock", .version = "1.0"},
      }
          .ToJson();
    }
    FAIL("tools/list must not be called after cancel");
    return Json{};
  });

  McpServerSession session(MakeConfig(), &transport);
  session.Start();

  REQUIRE(WaitUntil([&] { return request_received.load(); }, 2s));
  session.Stop();

  REQUIRE(session.State() == ServerState::Disconnected);
  REQUIRE(!session.Capabilities().has_tools);
}

}  // namespace yac::mcp::test
