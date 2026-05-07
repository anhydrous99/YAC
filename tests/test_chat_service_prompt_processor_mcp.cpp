#include "chat/chat_service_mcp.hpp"
#include "chat/chat_service_prompt_processor.hpp"
#include "chat/tool_approval_manager.hpp"
#include "lambda_mock_provider.hpp"
#include "mock_mcp_manager.hpp"
#include "provider/language_model_provider.hpp"
#include "provider/provider_registry.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/todo_state.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <mutex>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::chat::internal;
using namespace yac::provider;
using namespace yac::tool_call;
using yac::test::MockMcpManager;
using yac::testing::LambdaMockProvider;

namespace {

std::shared_ptr<LambdaMockProvider> MakeTwoRoundToolProvider(
    std::string tool_name, std::string arguments_json,
    std::string expected_catalog_tool, std::shared_ptr<bool> saw_catalog_tool,
    std::shared_ptr<int> request_count) {
  return std::make_shared<LambdaMockProvider>(
      "test-provider",
      [tool_name = std::move(tool_name),
       arguments_json = std::move(arguments_json),
       expected_catalog_tool = std::move(expected_catalog_tool),
       saw_catalog_tool,
       request_count](const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) {
        REQUIRE_FALSE(stop_token.stop_requested());
        ++(*request_count);
        if (*request_count == 1) {
          *saw_catalog_tool = std::ranges::any_of(
              request.tools, [&](const ToolDefinition& definition) {
                return definition.name == expected_catalog_tool;
              });
          sink(ChatEvent{
              ToolCallRequestedEvent{.tool_calls = {ToolCallRequest{
                                         .id = "tool-1",
                                         .name = tool_name,
                                         .arguments_json = arguments_json,
                                     }}}});
          return;
        }
        REQUIRE(std::ranges::any_of(
            request.messages, [](const ChatMessage& message) {
              return message.role == ChatRole::Tool &&
                     message.tool_call_id == ::yac::ToolCallId{"tool-1"};
            }));
        sink(ChatEvent{TextDeltaEvent{.text = "done"}});
      });
}

struct CountingBuiltInExecutor {
  int prepare_count = 0;
  int execute_count = 0;

  PreparedToolCall Prepare(const ToolCallRequest& request) {
    ++prepare_count;
    return ToolExecutor::Prepare(request);
  }

  ToolExecutionResult Execute(const PreparedToolCall& prepared,
                              std::stop_token stop_token) {
    (void)stop_token;
    ++execute_count;
    BashCall call;
    if (const auto* preview = std::get_if<BashCall>(&prepared.preview);
        preview != nullptr) {
      call = *preview;
    } else {
      call.command = prepared.request.name;
    }
    call.output = "ok";
    call.exit_code = 0;
    return ToolExecutionResult{.block = std::move(call),
                               .result_json = R"({"result":"ok"})",
                               .is_error = false};
  }
};

struct PromptProcessorHarness {
  explicit PromptProcessorHarness(
      std::shared_ptr<LanguageModelProvider> provider,
      MockMcpManager* mcp_manager, CountingBuiltInExecutor* built_in_executor,
      bool auto_approve = false)
      : workspace_root(std::filesystem::temp_directory_path() /
                       "yac_prompt_processor_mcp_tests"),
        tool_executor(workspace_root, nullptr, todo_state),
        mcp_helper(mcp_manager),
        processor(
            registry, tool_executor, tool_approval,
            mcp_manager != nullptr ? &mcp_helper : nullptr, history_mutex,
            history,
            [this, auto_approve](ChatEvent event) {
              if (auto_approve) {
                if (const auto* approval =
                        event.As<ToolApprovalRequestedEvent>();
                    approval != nullptr) {
                  tool_approval.ResolveToolApproval(approval->approval_id,
                                                    true);
                }
              }
              events.push_back(std::move(event));
            },
            [this] { return next_message_id++; }, [this] { return config; },
            [] { return uint64_t{1}; }, {}, nullptr, {},
            [built_in_executor](const ToolCallRequest& request) {
              return built_in_executor->Prepare(request);
            },
            [built_in_executor](const PreparedToolCall& prepared,
                                std::stop_token stop_token) {
              return built_in_executor->Execute(prepared, stop_token);
            }) {
    std::filesystem::create_directories(workspace_root);
    config.provider_id = provider->Id();
    config.model = "test-model";
    config.workspace_root = workspace_root.string();
    registry.Register(std::move(provider));
  }

  ~PromptProcessorHarness() { std::filesystem::remove_all(workspace_root); }
  PromptProcessorHarness(const PromptProcessorHarness&) = delete;
  PromptProcessorHarness& operator=(const PromptProcessorHarness&) = delete;
  PromptProcessorHarness(PromptProcessorHarness&&) = delete;
  PromptProcessorHarness& operator=(PromptProcessorHarness&&) = delete;

  std::filesystem::path workspace_root;
  ProviderRegistry registry;
  TodoState todo_state;
  ToolExecutor tool_executor;
  ToolApprovalManager tool_approval;
  ChatServiceMcp mcp_helper;
  std::mutex history_mutex;
  std::vector<ChatMessage> history;
  std::vector<ChatEvent> events;
  ChatConfig config;
  ChatMessageId next_message_id = 100;
  ChatServicePromptProcessor processor;
};

}  // namespace

TEST_CASE("mcp_route") {
  auto saw_catalog_tool = std::make_shared<bool>(false);
  auto request_count = std::make_shared<int>(0);
  auto provider = MakeTwoRoundToolProvider(
      "mcp_alpha__tool_a", R"({"value":1})", "mcp_alpha__tool_a",
      saw_catalog_tool, request_count);
  MockMcpManager mock_mcp_manager;
  mock_mcp_manager.AddTool("alpha", "tool_a");
  CountingBuiltInExecutor built_in_executor;
  PromptProcessorHarness harness(provider, &mock_mcp_manager,
                                 &built_in_executor);

  harness.processor.ProcessPrompt(1, "run mcp tool", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(*saw_catalog_tool);
  CHECK(mock_mcp_manager.invoke_count == 1);
  CHECK(built_in_executor.execute_count == 0);
}

TEST_CASE("builtin_unchanged") {
  auto saw_catalog_tool = std::make_shared<bool>(false);
  auto request_count = std::make_shared<int>(0);
  auto provider = MakeTwoRoundToolProvider(
      "bash", R"({"command":"pwd"})", "bash", saw_catalog_tool, request_count);
  MockMcpManager mock_mcp_manager;
  mock_mcp_manager.AddTool("alpha", "tool_a");
  CountingBuiltInExecutor built_in_executor;
  PromptProcessorHarness harness(provider, &mock_mcp_manager,
                                 &built_in_executor, true);

  harness.processor.ProcessPrompt(1, "run bash tool", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(*saw_catalog_tool);
  CHECK(built_in_executor.execute_count == 1);
  CHECK(mock_mcp_manager.invoke_count == 0);
}

TEST_CASE("approval_from_snapshot") {
  auto saw_catalog_tool = std::make_shared<bool>(false);
  auto request_count = std::make_shared<int>(0);
  auto provider = MakeTwoRoundToolProvider(
      "mcp_alpha__tool_a", R"({"value":2})", "mcp_alpha__tool_a",
      saw_catalog_tool, request_count);
  MockMcpManager mock_mcp_manager;
  mock_mcp_manager.AddTool("alpha", "tool_a", true);
  CountingBuiltInExecutor built_in_executor;
  PromptProcessorHarness harness(provider, &mock_mcp_manager,
                                 &built_in_executor, true);

  harness.processor.ProcessPrompt(1, "run approved mcp tool", 1,
                                  std::stop_source{}.get_token());

  const auto approval_it =
      std::ranges::find_if(harness.events, [](const ChatEvent& event) {
        return event.Type() == ChatEventType::ToolApprovalRequested;
      });
  REQUIRE(approval_it != harness.events.end());
  CHECK(approval_it->Get<ToolApprovalRequestedEvent>().tool_name ==
        "mcp_alpha__tool_a");
  CHECK(mock_mcp_manager.invoke_count == 1);
  CHECK(built_in_executor.execute_count == 0);
}
