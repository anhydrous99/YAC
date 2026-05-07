#include "chat/chat_service.hpp"
#include "chat/config.hpp"
#include "chat/types.hpp"
#include "lambda_mock_provider.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_registry.hpp"

#include <condition_variable>
#include <mutex>
#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using yac::testing::LambdaMockProvider;

struct HeadlessResult {
  std::string output;
  std::string error_output;
  int exit_code = 0;
};

HeadlessResult RunWithProvider(std::shared_ptr<LanguageModelProvider> provider,
                               const std::string& prompt,
                               bool auto_approve = false) {
  ProviderRegistry registry;
  registry.Register(provider);
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"fake"};
  config.model = ::yac::ModelId{"fake-model"};
  ChatService service(std::move(registry), config);

  HeadlessResult result;
  std::mutex done_mutex;
  std::condition_variable done_cv;
  bool done = false;

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
            result.error_output = e.text;
            result.exit_code = 1;
            std::unique_lock<std::mutex> lock(done_mutex);
            done = true;
            done_cv.notify_one();
          } else if constexpr (std::is_same_v<T, ToolApprovalRequestedEvent>) {
            service.ResolveToolApproval(e.approval_id, auto_approve);
            if (!auto_approve) {
              result.exit_code = 1;
              std::unique_lock<std::mutex> lock(done_mutex);
              done = true;
              done_cv.notify_one();
            }
          }
        },
        event.payload);
  });

  service.SubmitUserMessage(prompt);
  std::unique_lock<std::mutex> lock(done_mutex);
  done_cv.wait(lock, [&] { return done; });

  return result;
}

TEST_CASE("Headless event handler: text deltas accumulate to output") {
  auto provider = std::make_shared<LambdaMockProvider>(
      "fake",
      [](const ChatRequest&, ChatEventSink sink, std::stop_token stop_token) {
        if (stop_token.stop_requested()) {
          return;
        }
        sink(ChatEvent{TextDeltaEvent{.text = "hello"}});
        sink(ChatEvent{TextDeltaEvent{.text = " world"}});
      });
  auto result = RunWithProvider(std::move(provider), "hello");
  REQUIRE(result.output == "hello world");
  REQUIRE(result.exit_code == 0);
  REQUIRE(result.error_output.empty());
}

TEST_CASE("Headless event handler: error provider sets exit code 1") {
  auto provider = std::make_shared<LambdaMockProvider>(
      "fake", [](const ChatRequest&, ChatEventSink sink, std::stop_token) {
        sink(ChatEvent{ErrorEvent{.text = "provider error"}});
      });
  auto result = RunWithProvider(std::move(provider), "hello");
  REQUIRE(result.exit_code == 1);
  REQUIRE(!result.error_output.empty());
}
