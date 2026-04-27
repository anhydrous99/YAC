#include "app/chat_event_bridge.hpp"
#include "presentation/chat_ui.hpp"

#include <functional>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::app;
using namespace yac::chat;
using namespace yac::presentation;

TEST_CASE("mcp_state_changed_posts") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  int post_count = 0;
  bridge.SetPostFn([&post_count](std::function<void()> fn) {
    ++post_count;
    fn();
  });

  bridge.HandleEvent(ChatEvent{McpServerStateChangedEvent{
      .server_id = "my-server", .state = "connected", .error = ""}});

  REQUIRE(post_count == 1);
  auto snapshot = bridge.GetMcpStatusSink().GetSnapshot();
  REQUIRE(snapshot.size() == 1);
  REQUIRE(snapshot[0].id == "my-server");
  REQUIRE(snapshot[0].state == "connected");
  REQUIRE(snapshot[0].error.empty());
}

TEST_CASE("progress_no_race") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  constexpr int kIterations = 50;
  std::vector<std::jthread> threads;
  threads.reserve(kIterations);

  for (int i = 0; i < kIterations; ++i) {
    threads.emplace_back([&bridge, i] {
      bridge.HandleEvent(ChatEvent{McpProgressUpdateEvent{
          .message_id = 1,
          .text = std::to_string(i),
          .progress = static_cast<double>(i),
          .total = static_cast<double>(kIterations),
      }});
    });
  }
  threads.clear();

  auto& sink = bridge.GetMcpStatusSink();
  auto progress = sink.GetProgress();
  REQUIRE(progress.total == static_cast<double>(kIterations));
}
