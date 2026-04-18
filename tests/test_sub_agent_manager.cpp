#include "chat/chat_service_tool_approval.hpp"
#include "chat/sub_agent_manager.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using namespace yac::tool_call;

namespace {

class InstantMockProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "instant-mock"; }

  void CompleteStream([[maybe_unused]] const ChatRequest& request,
                      ChatEventSink sink,
                      [[maybe_unused]] std::stop_token stop) override {
    sink(ChatEvent{.type = ChatEventType::TextDelta, .text = "done"});
  }
};

class BlockingMockProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "blocking-mock"; }

  void CompleteStream([[maybe_unused]] const ChatRequest& request,
                      [[maybe_unused]] ChatEventSink sink,
                      std::stop_token stop) override {
    while (!stop.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
};

class SleepingMockProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "sleeping-mock"; }

  void CompleteStream([[maybe_unused]] const ChatRequest& request,
                      [[maybe_unused]] ChatEventSink sink,
                      std::stop_token stop) override {
    for (int i = 0; i < 20 && !stop.stop_requested(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
  }
};

class PeriodicEventMockProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override {
    return "periodic-event-mock";
  }

  void CompleteStream([[maybe_unused]] const ChatRequest& request,
                      ChatEventSink sink, std::stop_token stop) override {
    while (!stop.stop_requested()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(200));
      if (stop.stop_requested()) {
        break;
      }
      sink(ChatEvent{.type = ChatEventType::TextDelta, .text = "tick"});
    }
  }
};

struct SubAgentTestContext {
  std::filesystem::path workspace;
  ProviderRegistry registry;
  std::shared_ptr<ToolExecutor> executor;
  internal::ChatServiceToolApproval tool_approval;
  std::atomic<ChatMessageId> next_id{1};
  ChatConfig config;
  std::unique_ptr<SubAgentManager> manager;

  explicit SubAgentTestContext(
      std::shared_ptr<LanguageModelProvider> prov,
      int timeout_seconds = kDefaultSubAgentTimeoutSeconds) {
    static std::atomic<int> counter{0};
    workspace =
        std::filesystem::temp_directory_path() /
        ("yac_sam_test_" + std::to_string(counter.fetch_add(1)));
    std::filesystem::create_directories(workspace);
    config.provider_id = prov->Id();
    config.model = "test-model";
    registry.Register(std::move(prov));
    executor = std::make_shared<ToolExecutor>(workspace, nullptr);
    manager = std::make_unique<SubAgentManager>(
        registry, executor, tool_approval,
        [](ChatEvent) {},
        [this]() { return config; },
        [this]() -> ChatMessageId { return next_id.fetch_add(1); },
        timeout_seconds);
  }

  ~SubAgentTestContext() {
    manager.reset();
    std::filesystem::remove_all(workspace);
  }

  SubAgentTestContext(const SubAgentTestContext&) = delete;
  SubAgentTestContext& operator=(const SubAgentTestContext&) = delete;
  SubAgentTestContext(SubAgentTestContext&&) = delete;
  SubAgentTestContext& operator=(SubAgentTestContext&&) = delete;
};

}  // namespace

TEST_CASE("SubAgentManager enforces concurrency limit") {
  SubAgentTestContext ctx(std::make_shared<BlockingMockProvider>());

  for (int i = 0; i < kMaxConcurrentSubAgents; ++i) {
    const auto id =
        ctx.manager->SpawnBackground("task " + std::to_string(i));
    REQUIRE(id.find("capacity") == std::string::npos);
  }

  const auto overflow = ctx.manager->SpawnBackground("overflow task");
  REQUIRE((overflow.find("capacity") != std::string::npos ||
           overflow.find("reached") != std::string::npos));

  ctx.manager->CancelAll();
}

TEST_CASE("SubAgentManager generates unique agent IDs") {
  SubAgentTestContext ctx(std::make_shared<BlockingMockProvider>());

  const auto id1 = ctx.manager->SpawnBackground("task 1");
  const auto id2 = ctx.manager->SpawnBackground("task 2");
  const auto id3 = ctx.manager->SpawnBackground("task 3");

  REQUIRE(id1 != id2);
  REQUIRE(id2 != id3);
  REQUIRE(id1 != id3);

  ctx.manager->CancelAll();
}

TEST_CASE("SpawnBackground returns immediately") {
  SubAgentTestContext ctx(std::make_shared<SleepingMockProvider>());

  const auto before = std::chrono::steady_clock::now();
  const auto id = ctx.manager->SpawnBackground("sleeping task");
  const auto after = std::chrono::steady_clock::now();

  const auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(after - before)
          .count();

  REQUIRE_FALSE(id.empty());
  REQUIRE(elapsed_ms < 50);

  ctx.manager->CancelAll();
}

TEST_CASE("CancelAll stops all active sessions") {
  SubAgentTestContext ctx(std::make_shared<BlockingMockProvider>());

  [[maybe_unused]] const auto a = ctx.manager->SpawnBackground("task 1");
  [[maybe_unused]] const auto b = ctx.manager->SpawnBackground("task 2");

  ctx.manager->CancelAll();

  REQUIRE_FALSE(ctx.manager->IsAtCapacity());
}

TEST_CASE("Background timeout triggers cancellation") {
  constexpr int kTimeoutSeconds = 2;
  SubAgentTestContext ctx(std::make_shared<PeriodicEventMockProvider>(),
                          kTimeoutSeconds);

  [[maybe_unused]] const auto agent =
      ctx.manager->SpawnBackground("long running task");

  std::this_thread::sleep_for(std::chrono::seconds(3));

  REQUIRE_FALSE(ctx.manager->IsAtCapacity());
}
