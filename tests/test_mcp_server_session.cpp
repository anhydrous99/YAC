#include "mcp/mcp_server_session.hpp"
#include "mcp/protocol_constants.hpp"
#include "mock_mcp_transport.hpp"

#include <algorithm>
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

  REQUIRE(!states.empty());
  REQUIRE(states.front() == ServerState::Disconnected);
  REQUIRE(states.back() == ServerState::Ready);
  REQUIRE(std::ranges::find(states, ServerState::Initializing) != states.end());
  REQUIRE(session.Tools()->size() == 2);
  REQUIRE(session.Resources()->empty());
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
  REQUIRE(session.Tools()->empty());
  REQUIRE(session.Resources()->size() == 1);
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
  REQUIRE(session.Tools()->size() == 1);
  REQUIRE((*session.Tools())[0].name == "read");
  REQUIRE(session.Resources()->size() == 1);
  REQUIRE((*session.Resources())[0].uri == "file:///one.txt");

  transport.EmitNotification(pc::kMethodNotificationsToolsListChanged,
                             Json::object());
  transport.EmitNotification(pc::kMethodNotificationsResourcesListChanged,
                             Json::object());
  session.RefreshIfDirty();

  REQUIRE(session.Tools()->size() == 1);
  REQUIRE((*session.Tools())[0].name == "write");
  REQUIRE(session.Resources()->size() == 1);
  REQUIRE((*session.Resources())[0].uri == "file:///two.txt");

  session.Stop();
}

TEST_CASE("list_changed_lazy") {
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
      ++tools_list_calls;
      return ToolsListResponse{
          .tools = {ToolDefinition{.name = "read"}},
      }
          .ToJson();
    }
    if (method == pc::kMethodResourcesList) {
      ++resources_list_calls;
      return ResourcesListResponse{
          .resources = {ResourceDescriptor{.uri = "file:///one.txt"}},
      }
          .ToJson();
    }
    throw std::runtime_error("unexpected request");
  });

  McpServerSession session(MakeConfig(), &transport);
  session.Start();

  REQUIRE(WaitUntil([&] { return session.State() == ServerState::Ready; }));
  REQUIRE(session.Tools()->size() == 1);
  REQUIRE(session.Resources()->size() == 1);
  REQUIRE(transport.RecordedRequests().size() == 3);

  transport.EmitNotification(pc::kMethodNotificationsToolsListChanged,
                             Json::object());
  transport.EmitNotification(pc::kMethodNotificationsResourcesListChanged,
                             Json::object());
  REQUIRE(transport.RecordedRequests().size() == 3);

  session.RefreshIfDirty();

  REQUIRE(transport.RecordedRequests().size() == 5);
  REQUIRE(transport.RecordedRequests()[3].method ==
          std::string(pc::kMethodToolsList));
  REQUIRE(transport.RecordedRequests()[4].method ==
          std::string(pc::kMethodResourcesList));
  REQUIRE(session.Tools()->size() == 1);
  REQUIRE((*session.Tools())[0].name == "read");
  REQUIRE(session.Resources()->size() == 1);
  REQUIRE((*session.Resources())[0].uri == "file:///one.txt");

  REQUIRE(tools_list_calls == 2);
  REQUIRE(resources_list_calls == 2);

  session.Stop();
}

TEST_CASE("refresh_atomic_replace") {
  MockMcpTransport transport;
  std::atomic<int> tools_list_calls{0};
  std::atomic<bool> refresh_started{false};
  std::promise<void> allow_refresh;
  auto allow_refresh_future = allow_refresh.get_future().share();
  transport.SetRequestHandler(
      [&refresh_started, allow_refresh_future = std::move(allow_refresh_future),
       &tools_list_calls](std::string_view method, const Json& params,
                          std::chrono::milliseconds timeout,
                          std::stop_token stop) -> Json {
        (void)params;
        (void)timeout;
        (void)stop;
        if (method == pc::kMethodInitialize) {
          return MakeInitializeResponse(std::string(pc::kMcpProtocolVersion),
                                        ServerCapabilities{.has_tools = true})
              .ToJson();
        }
        if (method == pc::kMethodToolsList) {
          const int call = ++tools_list_calls;
          if (call == 2) {
            refresh_started = true;
            allow_refresh_future.wait();
          }
          return ToolsListResponse{
              .tools = {ToolDefinition{.name = call == 1 ? "old-a" : "new-a"},
                        ToolDefinition{.name = call == 1 ? "old-b" : "new-b"}},
          }
              .ToJson();
        }
        throw std::runtime_error("unexpected request");
      });

  McpServerSession session(MakeConfig(), &transport);
  session.Start();

  REQUIRE(WaitUntil([&] { return session.State() == ServerState::Ready; }));
  REQUIRE(session.Tools()->size() == 2);
  REQUIRE((*session.Tools())[0].name == "old-a");
  REQUIRE((*session.Tools())[1].name == "old-b");

  transport.EmitNotification(pc::kMethodNotificationsToolsListChanged,
                             Json::object());

  std::atomic<bool> saw_mixed_snapshot{false};
  std::atomic<bool> refresh_done{false};
  std::jthread reader([&](std::stop_token stop_token) {
    while (!stop_token.stop_requested() && !refresh_done.load()) {
      const auto tools = session.Tools();
      if (tools->size() != 2) {
        saw_mixed_snapshot = true;
        return;
      }
      const bool old_snapshot =
          (*tools)[0].name == "old-a" && (*tools)[1].name == "old-b";
      const bool new_snapshot =
          (*tools)[0].name == "new-a" && (*tools)[1].name == "new-b";
      if (!old_snapshot && !new_snapshot) {
        saw_mixed_snapshot = true;
        return;
      }
      std::this_thread::yield();
    }
  });

  std::thread refresher([&] {
    session.RefreshIfDirty();
    refresh_done = true;
  });

  REQUIRE(WaitUntil([&] { return refresh_started.load(); }));
  allow_refresh.set_value();
  refresher.join();
  reader.request_stop();
  reader.join();

  REQUIRE_FALSE(saw_mixed_snapshot.load());
  REQUIRE(session.Tools()->size() == 2);
  REQUIRE((*session.Tools())[0].name == "new-a");
  REQUIRE((*session.Tools())[1].name == "new-b");
  REQUIRE(tools_list_calls == 2);

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
      std::scoped_lock lock(times_mutex);
      attempt_times.push_back(std::chrono::steady_clock::now());
      ++attempt_count;
      throw std::runtime_error("connection refused");
    }
    throw std::runtime_error("unexpected request");
  });

  const auto k_initial_delay = std::chrono::milliseconds{100};
  McpServerSession session(MakeConfig(), &transport, nullptr, k_initial_delay);
  session.Start();

  REQUIRE(
      WaitUntil([&] { return session.State() == ServerState::Failed; }, 5s));
  REQUIRE(attempt_count == pc::kReconnectMaxAttempts);
  REQUIRE(static_cast<int>(attempt_times.size()) == pc::kReconnectMaxAttempts);

  auto expected_delay = k_initial_delay;
  for (size_t i = 1; i < attempt_times.size(); ++i) {
    const auto actual_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            attempt_times[i] - attempt_times[i - 1])
            .count();
    const auto base_ms = expected_delay.count();
    // Generous slack absorbs scheduler jitter on slow CI runners (notably
    // macOS GitHub runners), where condvar wakeup + thread re-scheduling can
    // add 100ms+ on top of the expected delay + 25% jitter.
    CHECK(actual_ms >= base_ms - 100);
    CHECK(actual_ms <=
          static_cast<long long>(static_cast<double>(base_ms) * 1.25) + 500);
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

  const auto k_long_delay = std::chrono::milliseconds{5000};
  McpServerSession session(MakeConfig(), &transport, nullptr, k_long_delay);
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
