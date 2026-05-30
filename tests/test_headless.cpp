#include "chat/agent_mode.hpp"
#include "chat/chat_service.hpp"
#include "chat/types.hpp"
#include "lambda_mock_provider.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_registry.hpp"

#include <algorithm>
#include <condition_variable>
#include <filesystem>
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
                               bool auto_approve = false,
                               bool plan_mode = false) {
  ProviderRegistry registry;
  registry.Register(provider);
  ChatConfig config;
  config.provider_id = ::yac::ProviderId{"fake"};
  config.model = ::yac::ModelId{"fake-model"};
  const auto workspace_root =
      std::filesystem::temp_directory_path() / "yac_headless_plan_mode";
  std::filesystem::remove_all(workspace_root);
  std::filesystem::create_directories(workspace_root);
  config.workspace_root = workspace_root.string();
  if (plan_mode) {
    config.agent_mode = AgentMode::Plan;
    config.has_entered_plan_mode = true;
  }

  HeadlessResult result;
  std::mutex done_mutex;
  std::condition_variable done_cv;
  bool done = false;

  ChatService service(std::move(registry), config);

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
          } else if constexpr (std::is_same_v<T, ToolApprovalRequestedEvent>) {
            service.ResolveToolApproval(e.approval_id, auto_approve);
            if (!auto_approve) {
              result.exit_code = 1;
            }
          }
        },
        event.payload);
  });

  service.SubmitUserMessage(prompt);
  std::unique_lock<std::mutex> lock(done_mutex);
  done_cv.wait(lock, [&] { return done; });

  std::filesystem::remove_all(workspace_root);
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

TEST_CASE("Headless event handler: default run starts in Build mode") {
  auto provider = std::make_shared<LambdaMockProvider>(
      "fake", [](const ChatRequest& request, ChatEventSink sink,
                 std::stop_token stop_token) {
        if (stop_token.stop_requested()) {
          return;
        }
        REQUIRE(
            std::ranges::none_of(request.tools, [](const ToolDefinition& tool) {
              return tool.name == "plan_exit";
            }));
        const auto system_message = std::ranges::find_if(
            request.messages, [](const ChatMessage& message) {
              return message.role == ChatRole::System;
            });
        if (system_message != request.messages.end()) {
          REQUIRE(system_message->content.find("Plan mode") ==
                  std::string::npos);
        }
        sink(ChatEvent{TextDeltaEvent{.text = "build"}});
      });

  auto result = RunWithProvider(std::move(provider), "hello");
  REQUIRE(result.output == "build");
  REQUIRE(result.exit_code == 0);
}

TEST_CASE("Headless event handler: --plan entry starts in Plan mode") {
  auto provider = std::make_shared<LambdaMockProvider>(
      "fake", [](const ChatRequest& request, ChatEventSink sink,
                 std::stop_token stop_token) {
        if (stop_token.stop_requested()) {
          return;
        }
        REQUIRE(
            std::ranges::any_of(request.tools, [](const ToolDefinition& tool) {
              return tool.name == "plan_exit";
            }));
        REQUIRE(
            std::ranges::none_of(request.tools, [](const ToolDefinition& tool) {
              return tool.name == "file_write" || tool.name == "file_edit";
            }));
        const auto system_message = std::ranges::find_if(
            request.messages, [](const ChatMessage& message) {
              return message.role == ChatRole::System;
            });
        REQUIRE(system_message != request.messages.end());
        REQUIRE(system_message->content.find("Plan mode") != std::string::npos);
        REQUIRE(system_message->content.find("plan_exit") != std::string::npos);
        sink(ChatEvent{TextDeltaEvent{.text = "plan"}});
      });

  auto result = RunWithProvider(std::move(provider), "hello", false, true);
  REQUIRE(result.output == "plan");
  REQUIRE(result.exit_code == 0);
}
