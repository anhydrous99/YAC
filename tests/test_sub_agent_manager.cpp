#include "chat/sub_agent_manager.hpp"
#include "chat/tool_approval_manager.hpp"
#include "lambda_mock_provider.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/todo_state.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using namespace yac::tool_call;
using yac::testing::LambdaMockProvider;

namespace {

std::shared_ptr<LambdaMockProvider> MakeBlockingProvider() {
  return std::make_shared<LambdaMockProvider>(
      "blocking-mock",
      [](const ChatRequest&, ChatEventSink, std::stop_token stop) {
        while (!stop.stop_requested()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      });
}

std::shared_ptr<LambdaMockProvider> MakeSleepingProvider() {
  return std::make_shared<LambdaMockProvider>(
      "sleeping-mock",
      [](const ChatRequest&, ChatEventSink, std::stop_token stop) {
        for (int i = 0; i < 20 && !stop.stop_requested(); ++i) {
          std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
      });
}

std::shared_ptr<LambdaMockProvider> MakePeriodicEventProvider() {
  return std::make_shared<LambdaMockProvider>(
      "periodic-event-mock",
      [](const ChatRequest&, ChatEventSink sink, std::stop_token stop) {
        while (!stop.stop_requested()) {
          std::this_thread::sleep_for(std::chrono::milliseconds(200));
          if (stop.stop_requested()) {
            break;
          }
          sink(ChatEvent{TextDeltaEvent{.text = "tick"}});
        }
      });
}

std::shared_ptr<LambdaMockProvider> MakeToolRequestProvider() {
  return std::make_shared<LambdaMockProvider>(
      "tool-request-mock",
      [](const ChatRequest& request, ChatEventSink sink, std::stop_token stop) {
        if (stop.stop_requested()) {
          return;
        }
        const bool has_tool_result = std::ranges::any_of(
            request.messages, [](const ChatMessage& message) {
              return message.role == ChatRole::Tool;
            });
        if (!has_tool_result) {
          sink(ChatEvent{ToolCallRequestedEvent{
              .tool_calls = {
                  ToolCallRequest{.id = "tool-1",
                                  .name = std::string(kListDirToolName),
                                  .arguments_json = R"({"path":"."})"}}}});
          return;
        }
        sink(ChatEvent{TextDeltaEvent{.text = "final answer"}});
      });
}

template <typename Predicate>
bool WaitUntil(Predicate predicate, std::chrono::milliseconds timeout) {
  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    if (predicate()) {
      return true;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }
  return predicate();
}

struct SubAgentTestContext {
  std::filesystem::path workspace;
  ProviderRegistry registry;
  TodoState todo_state;
  std::shared_ptr<ToolExecutor> executor;
  ToolApprovalManager tool_approval;
  std::atomic<ChatMessageId> next_id{1};
  ChatConfig config;
  std::mutex events_mutex;
  std::vector<ChatEvent> events;
  std::unique_ptr<SubAgentManager> manager;

  explicit SubAgentTestContext(
      std::shared_ptr<LanguageModelProvider> prov,
      int timeout_seconds = kDefaultSubAgentTimeoutSeconds) {
    static std::atomic<int> counter{0};
    workspace = std::filesystem::temp_directory_path() /
                ("yac_sam_test_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(workspace);
    config.provider_id = prov->Id();
    config.model = "test-model";
    registry.Register(std::move(prov));
    executor = std::make_shared<ToolExecutor>(workspace, nullptr, todo_state);
    manager = std::make_unique<SubAgentManager>(
        registry, executor, tool_approval,
        [this](ChatEvent event) {
          std::scoped_lock lock(events_mutex);
          events.push_back(std::move(event));
        },
        [this]() { return config; }, timeout_seconds);
  }

  std::string Spawn(const std::string& task) {
    const auto id = next_id.fetch_add(1);
    return manager->SpawnBackground(
        task, id, ::yac::ToolCallId{"tc-" + std::to_string(id)});
  }

  ~SubAgentTestContext() {
    manager.reset();
    std::filesystem::remove_all(workspace);
  }

  SubAgentTestContext(const SubAgentTestContext&) = delete;
  SubAgentTestContext& operator=(const SubAgentTestContext&) = delete;
  SubAgentTestContext(SubAgentTestContext&&) = delete;
  SubAgentTestContext& operator=(SubAgentTestContext&&) = delete;

  std::vector<ChatEvent> SnapshotEvents() {
    std::scoped_lock lock(events_mutex);
    return events;
  }
};

}  // namespace

TEST_CASE("SubAgentManager enforces concurrency limit") {
  SubAgentTestContext ctx(MakeBlockingProvider());

  for (int i = 0; i < kMaxConcurrentSubAgents; ++i) {
    const auto id = ctx.Spawn("task " + std::to_string(i));
    REQUIRE(id.find("capacity") == std::string::npos);
  }

  const auto overflow = ctx.Spawn("overflow task");
  REQUIRE((overflow.find("capacity") != std::string::npos ||
           overflow.find("reached") != std::string::npos));

  ctx.manager->CancelAll();
}

TEST_CASE("SubAgentManager generates unique agent IDs") {
  SubAgentTestContext ctx(MakeBlockingProvider());

  const auto id1 = ctx.Spawn("task 1");
  const auto id2 = ctx.Spawn("task 2");
  const auto id3 = ctx.Spawn("task 3");

  REQUIRE(id1 != id2);
  REQUIRE(id2 != id3);
  REQUIRE(id1 != id3);

  ctx.manager->CancelAll();
}

TEST_CASE("SpawnBackground returns immediately") {
  SubAgentTestContext ctx(MakeSleepingProvider());

  const auto before = std::chrono::steady_clock::now();
  const auto id = ctx.Spawn("sleeping task");
  const auto after = std::chrono::steady_clock::now();

  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(after - before)
          .count();

  REQUIRE_FALSE(id.empty());
  REQUIRE(elapsed_ms < 50);

  ctx.manager->CancelAll();
}

TEST_CASE("CancelAll stops all active sessions") {
  SubAgentTestContext ctx(MakeBlockingProvider());

  // NOLINTNEXTLINE(bugprone-unused-local-non-trivial-variable)
  [[maybe_unused]] const auto a = ctx.Spawn("task 1");
  // NOLINTNEXTLINE(bugprone-unused-local-non-trivial-variable)
  [[maybe_unused]] const auto b = ctx.Spawn("task 2");

  ctx.manager->CancelAll();

  REQUIRE_FALSE(ctx.manager->IsAtCapacity());
}

TEST_CASE("Background timeout triggers cancellation") {
  constexpr int kTimeoutSeconds = 2;
  SubAgentTestContext ctx(MakePeriodicEventProvider(), kTimeoutSeconds);

  // NOLINTNEXTLINE(bugprone-unused-local-non-trivial-variable)
  [[maybe_unused]] const auto agent = ctx.Spawn("long running task");

  std::this_thread::sleep_for(std::chrono::seconds(3));

  REQUIRE_FALSE(ctx.manager->IsAtCapacity());
}

TEST_CASE("Background sub-agent reports tool progress") {
  SubAgentTestContext ctx(MakeToolRequestProvider());

  const auto agent = ctx.Spawn("inspect workspace");
  REQUIRE(agent.find("capacity") == std::string::npos);

  REQUIRE(WaitUntil(
      [&ctx] {
        const auto events = ctx.SnapshotEvents();
        return std::ranges::any_of(events, [](const ChatEvent& event) {
          return event.Type() == ChatEventType::SubAgentCompleted;
        });
      },
      std::chrono::milliseconds(1000)));

  const auto events = ctx.SnapshotEvents();
  const auto progress =
      std::ranges::find_if(events, [](const ChatEvent& event) {
        const auto* progress = event.As<SubAgentProgressEvent>();
        return progress != nullptr && progress->sub_agent_tool_count == 1;
      });
  REQUIRE(progress != events.end());

  const auto completion =
      std::ranges::find_if(events, [](const ChatEvent& event) {
        return event.Type() == ChatEventType::SubAgentCompleted;
      });
  REQUIRE(completion != events.end());
  const auto& completed = completion->Get<SubAgentCompletedEvent>();
  REQUIRE(completed.sub_agent_result == "final answer");
  REQUIRE(completed.sub_agent_tool_count == 1);
}
