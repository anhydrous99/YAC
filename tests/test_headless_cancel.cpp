#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/types.hpp"
#include "lambda_mock_provider.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_registry.hpp"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using yac::testing::LambdaMockProvider;

namespace {

std::shared_ptr<LambdaMockProvider> MakeSlowProvider() {
  return std::make_shared<LambdaMockProvider>(
      "fake",
      [](const ChatRequest&, ChatEventSink sink, std::stop_token stop_token) {
        for (int i = 0; i < 100; ++i) {
          if (stop_token.stop_requested()) {
            return;
          }
          // SLEEP-RATIONALE: deliberately slow provider; exercises cancellation
          // window timing
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
          sink(ChatEvent{TextDeltaEvent{.text = "x"}});
        }
        sink(ChatEvent{FinishedEvent{}});
      });
}

struct CancelTestResult {
  std::string output;
  std::chrono::milliseconds elapsed{};
  bool was_cancelled = false;
};

CancelTestResult RunWithCancelTimer(int cancel_after_ms) {
  ProviderRegistry registry;
  registry.Register(MakeSlowProvider());
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"fake"};
  config.model = ::yac::ModelId{"fake-model"};
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
      // SLEEP-RATIONALE: this IS the cancel timer — wall-clock delay is the
      // behaviour under test
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
