#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/types.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_registry.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

namespace {

class SlowProvider : public LanguageModelProvider {
 public:
  [[nodiscard]] std::string Id() const override { return "fake"; }
  void CompleteStream(const ChatRequest&, ChatEventSink sink,
                      std::stop_token stop_token) override {
    for (int i = 0; i < 100; ++i) {
      if (stop_token.stop_requested()) {
        return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      sink(ChatEvent{TextDeltaEvent{.text = "x"}});
    }
    sink(ChatEvent{FinishedEvent{}});
  }
};

struct CancelTestResult {
  std::string output;
  std::chrono::milliseconds elapsed;
  bool was_cancelled = false;
};

CancelTestResult RunWithCancelTimer(int cancel_after_ms) {
  ProviderRegistry registry;
  registry.Register(std::make_shared<SlowProvider>());
  ChatConfig config;
  config.provider_id = "fake";
  config.model = "fake-model";
  ChatService service(std::move(registry), config);

  CancelTestResult result;
  std::mutex done_mutex;
  std::condition_variable done_cv;
  bool done = false;

  auto start = std::chrono::steady_clock::now();

  service.SetEventCallback([&](ChatEvent event) {
    std::visit(
        [&](auto&& e) {
          using T = std::decay_t<decltype(e)>;
          if constexpr (std::is_same_v<T, TextDeltaEvent>) {
            result.output += e.text;
          } else if constexpr (std::is_same_v<T, FinishedEvent>) {
            std::unique_lock<std::mutex> lock(done_mutex);
            done = true;
            done_cv.notify_one();
          } else if constexpr (std::is_same_v<T, ErrorEvent>) {
            result.was_cancelled = true;
            std::unique_lock<std::mutex> lock(done_mutex);
            done = true;
            done_cv.notify_one();
          }
        },
        event.payload);
  });

  service.SubmitUserMessage("test");

  std::thread cancel_timer;
  if (cancel_after_ms > 0) {
    cancel_timer = std::thread([&service, cancel_after_ms]() {
      std::this_thread::sleep_for(std::chrono::milliseconds(cancel_after_ms));
      service.CancelActiveResponse();
    });
  }

  std::unique_lock<std::mutex> lock(done_mutex);
  done_cv.wait(lock, [&] { return done; });

  auto end = std::chrono::steady_clock::now();
  result.elapsed =
      std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

  if (cancel_timer.joinable()) {
    cancel_timer.join();
  }

  return result;
}

}  // namespace

TEST_CASE("Headless cancel timer: flag_triggers_stop") {
  auto result = RunWithCancelTimer(200);
  REQUIRE(result.elapsed.count() >= 150);
  REQUIRE(result.elapsed.count() <= 500);
  REQUIRE(result.output.length() < 100);
}

TEST_CASE("Headless cancel timer: no cancel runs to completion") {
  auto result = RunWithCancelTimer(0);
  REQUIRE(result.output.length() == 100);
}
