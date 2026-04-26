#include "mcp/mcp_server_session.hpp"
#include "mcp/protocol_constants.hpp"
#include "mock_mcp_transport.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include <catch2/catch_approx.hpp>
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

InitializeResponse MakeInitializeResponse(std::string protocol_version,
                                          ServerCapabilities capabilities) {
  return InitializeResponse{
      .protocol_version = std::move(protocol_version),
      .capabilities = capabilities,
      .server_info =
          ImplementationInfo{.name = "mock-server", .version = "1.0.0"},
  };
}

}  // namespace

TEST_CASE("cold_start_happy_path") {
  MockMcpTransport transport;
  std::promise<void> continue_initialize;
  auto continue_initialize_future = continue_initialize.get_future().share();
  std::atomic<bool> done{false};

  transport.SetRequestHandler(
      [continue_initialize_future = std::move(continue_initialize_future)](
          std::string_view method, const Json& params,
          std::chrono::milliseconds timeout, std::stop_token stop) -> Json {
        (void)params;
        (void)timeout;
        (void)stop;
        if (method == pc::kMethodInitialize) {
          continue_initialize_future.wait();
          return MakeInitializeResponse(std::string(pc::kMcpProtocolVersion),
                                        ServerCapabilities{.has_tools = true})
              .ToJson();
        }
        if (method == pc::kMethodToolsList) {
          return ToolsListResponse{
              .tools = {ToolDefinition{.name = "search"},
                        ToolDefinition{.name = "read"}},
          }
              .ToJson();
        }
        throw std::runtime_error("unexpected request");
      });

  McpServerSession session(MakeConfig(), &transport);
  std::vector<ServerState> states{session.State()};
  std::jthread observer([&](std::stop_token stop_token) {
    ServerState last = states.back();
    while (!stop_token.stop_requested()) {
      const ServerState current = session.State();
      if (current != last) {
        states.push_back(current);
        last = current;
      }
      if (done.load()) {
        break;
      }
      std::this_thread::yield();
    }
  });

  session.Start();

  REQUIRE(WaitUntil(
      [&] { return session.State() == ServerState::Initializing; }, 2s));
  continue_initialize.set_value();
  REQUIRE(WaitUntil([&] { return session.State() == ServerState::Ready; }, 2s));
  done = true;
  observer.request_stop();
  observer.join();

  REQUIRE(states == std::vector<ServerState>{
                        ServerState::Disconnected, ServerState::Connecting,
                        ServerState::Initializing, ServerState::Ready});
  REQUIRE(session.Tools().size() == 2);
  REQUIRE(session.Resources().empty());
  REQUIRE(transport.RecordedNotifications().size() == 1);
  REQUIRE(transport.RecordedNotifications()[0].method ==
          std::string(pc::kMethodInitialized));

  session.Stop();
}

TEST_CASE("refuses_newer_protocol") {
  MockMcpTransport transport;
  transport.AddCannedResponse(
      std::string(pc::kMethodInitialize),
      MakeInitializeResponse("9999-12-31", ServerCapabilities{}).ToJson());

  McpServerSession session(MakeConfig(), &transport);
  session.Start();

  REQUIRE(WaitUntil([&] { return session.State() == ServerState::Failed; }));
  REQUIRE(session.LastError().find("protocol version") != std::string::npos);

  session.Stop();
}

TEST_CASE("accepts_older_protocol") {
  MockMcpTransport transport;
  transport.AddCannedResponse(
      std::string(pc::kMethodInitialize),
      MakeInitializeResponse("2024-11-05", ServerCapabilities{}).ToJson());

  McpServerSession session(MakeConfig(), &transport);
  session.Start();

  REQUIRE(WaitUntil([&] { return session.State() == ServerState::Ready; }));
  REQUIRE(session.LastError().empty());
  REQUIRE(!session.Capabilities().has_tools);
  REQUIRE(transport.RecordedRequests().size() == 1);

  session.Stop();
}

TEST_CASE("capability_gating_no_tools") {
  MockMcpTransport transport;
  transport.AddCannedResponse(
      std::string(pc::kMethodInitialize),
      MakeInitializeResponse(std::string(pc::kMcpProtocolVersion),
                             ServerCapabilities{.has_resources = true})
          .ToJson());
  transport.AddCannedResponse(
      std::string(pc::kMethodResourcesList),
      ResourcesListResponse{
          .resources = {ResourceDescriptor{.uri = "file:///notes.txt"}},
      }
          .ToJson());

  McpServerSession session(MakeConfig(), &transport);
  session.Start();

  REQUIRE(WaitUntil([&] { return session.State() == ServerState::Ready; }));
  REQUIRE(session.Tools().empty());
  REQUIRE(session.Resources().size() == 1);
  REQUIRE(transport.RecordedRequests().size() == 2);
  REQUIRE(transport.RecordedRequests()[0].method ==
          std::string(pc::kMethodInitialize));
  REQUIRE(transport.RecordedRequests()[1].method ==
          std::string(pc::kMethodResourcesList));

  session.Stop();
}

TEST_CASE("init_timeout") {
  MockMcpTransport transport;
  transport.SetRequestHandler([](std::string_view method, const Json& params,
                                 std::chrono::milliseconds timeout,
                                 std::stop_token stop) -> Json {
    (void)params;
    (void)timeout;
    (void)stop;
    if (method == pc::kMethodInitialize) {
      throw std::runtime_error("initialize timeout");
    }
    throw std::runtime_error("unexpected request");
  });

  McpServerSession session(MakeConfig(), &transport, nullptr,
                           std::chrono::milliseconds{10});
  session.Start();

  REQUIRE(
      WaitUntil([&] { return session.State() == ServerState::Failed; }, 2s));
  REQUIRE(session.LastError().find("timeout") != std::string::npos);

  session.Stop();
}

TEST_CASE("refresh_if_dirty_reloads_capability_gated_lists") {
  MockMcpTransport transport;
  std::atomic<int> tools_list_calls{0};
  std::atomic<int> resources_list_calls{0};
  transport.SetRequestHandler([&](std::string_view method, const Json& params,
                                  std::chrono::milliseconds timeout,
                                  std::stop_token stop) -> Json {
    (void)params;
    (void)timeout;
    (void)stop;
    if (method == pc::kMethodInitialize) {
      return MakeInitializeResponse(
                 std::string(pc::kMcpProtocolVersion),
                 ServerCapabilities{.has_tools = true, .has_resources = true})
          .ToJson();
    }
    if (method == pc::kMethodToolsList) {
      const int call = ++tools_list_calls;
      return ToolsListResponse{
          .tools = {ToolDefinition{.name = call == 1 ? "read" : "write"}},
      }
          .ToJson();
    }
    if (method == pc::kMethodResourcesList) {
      const int call = ++resources_list_calls;
      return ResourcesListResponse{
          .resources = {ResourceDescriptor{
              .uri = call == 1 ? "file:///one.txt" : "file:///two.txt"}},
      }
          .ToJson();
    }
    throw std::runtime_error("unexpected request");
  });

  McpServerSession session(MakeConfig(), &transport);
  session.Start();

  REQUIRE(WaitUntil([&] { return session.State() == ServerState::Ready; }));
  REQUIRE(session.Tools().size() == 1);
  REQUIRE(session.Tools()[0].name == "read");
  REQUIRE(session.Resources().size() == 1);
  REQUIRE(session.Resources()[0].uri == "file:///one.txt");

  transport.EmitNotification(pc::kMethodNotificationsToolsListChanged,
                             Json::object());
  transport.EmitNotification(pc::kMethodNotificationsResourcesListChanged,
                             Json::object());
  session.RefreshIfDirty();

  REQUIRE(session.Tools().size() == 1);
  REQUIRE(session.Tools()[0].name == "write");
  REQUIRE(session.Resources().size() == 1);
  REQUIRE(session.Resources()[0].uri == "file:///two.txt");

  session.Stop();
}

TEST_CASE("reconnect_backoff_schedule") {
  MockMcpTransport transport;
  std::atomic<int> attempt_count{0};
  std::vector<std::chrono::steady_clock::time_point> attempt_times;
  std::mutex times_mutex;

  transport.SetRequestHandler([&](std::string_view method, const Json& params,
                                  std::chrono::milliseconds timeout,
                                  std::stop_token stop) -> Json {
    (void)params;
    (void)timeout;
    (void)stop;
    if (method == pc::kMethodInitialize) {
      std::lock_guard lock(times_mutex);
      attempt_times.push_back(std::chrono::steady_clock::now());
      ++attempt_count;
      throw std::runtime_error("connection refused");
    }
    throw std::runtime_error("unexpected request");
  });

  const auto kInitialDelay = std::chrono::milliseconds{100};
  McpServerSession session(MakeConfig(), &transport, nullptr, kInitialDelay);
  session.Start();

  REQUIRE(
      WaitUntil([&] { return session.State() == ServerState::Failed; }, 5s));
  REQUIRE(attempt_count == pc::kReconnectMaxAttempts);
  REQUIRE(static_cast<int>(attempt_times.size()) == pc::kReconnectMaxAttempts);

  auto expected_delay = kInitialDelay;
  for (size_t i = 1; i < attempt_times.size(); ++i) {
    const auto actual_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            attempt_times[i] - attempt_times[i - 1])
            .count();
    const auto base_ms = expected_delay.count();
    CHECK(actual_ms >= base_ms - 100);
    CHECK(actual_ms <=
          static_cast<long long>(static_cast<double>(base_ms) * 1.25) + 100);
    expected_delay =
        std::min(std::chrono::duration_cast<std::chrono::milliseconds>(
                     expected_delay * pc::kReconnectBackoffMultiplier),
                 pc::kReconnectMaxDelayMs);
  }

  session.Stop();
}

TEST_CASE("cancel_aborts_backoff") {
  MockMcpTransport transport;
  std::atomic<bool> first_attempt_done{false};

  transport.SetRequestHandler([&](std::string_view method, const Json& params,
                                  std::chrono::milliseconds timeout,
                                  std::stop_token stop) -> Json {
    (void)params;
    (void)timeout;
    (void)stop;
    if (method == pc::kMethodInitialize) {
      first_attempt_done = true;
      throw std::runtime_error("connection refused");
    }
    throw std::runtime_error("unexpected request");
  });

  const auto kLongDelay = std::chrono::milliseconds{5000};
  McpServerSession session(MakeConfig(), &transport, nullptr, kLongDelay);
  session.Start();

  REQUIRE(WaitUntil(
      [&] {
        return first_attempt_done.load() &&
               session.State() == ServerState::Reconnecting;
      },
      2s));

  const auto before = std::chrono::steady_clock::now();
  session.Stop();
  const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                              std::chrono::steady_clock::now() - before)
                              .count();

  CHECK(elapsed_ms < 100);
}

}  // namespace yac::mcp::test
