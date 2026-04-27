#include "chat/chat_service_mcp.hpp"
#include "chat/chat_service_prompt_processor.hpp"
#include "chat/chat_service_tool_approval.hpp"
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

namespace {

class TwoRoundToolProvider : public LanguageModelProvider {
 public:
  TwoRoundToolProvider(std::string tool_name, std::string arguments_json,
                       std::string expected_catalog_tool)
      : tool_name_(std::move(tool_name)),
        arguments_json_(std::move(arguments_json)),
        expected_catalog_tool_(std::move(expected_catalog_tool)) {}

  [[nodiscard]] std::string Id() const override { return "test-provider"; }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop_token) override {
    REQUIRE_FALSE(stop_token.stop_requested());
    ++request_count_;
    if (request_count_ == 1) {
      saw_expected_catalog_tool_ =
          std::any_of(request.tools.begin(), request.tools.end(),
                      [&](const ToolDefinition& definition) {
                        return definition.name == expected_catalog_tool_;
                      });
      sink(ChatEvent{
          ToolCallRequestedEvent{.tool_calls = {ToolCallRequest{
                                     .id = "tool-1",
                                     .name = tool_name_,
                                     .arguments_json = arguments_json_,
                                 }}}});
      return;
    }

    REQUIRE(std::any_of(request.messages.begin(), request.messages.end(),
                        [](const ChatMessage& message) {
                          return message.role == ChatRole::Tool &&
                                 message.tool_call_id == "tool-1";
                        }));
    sink(ChatEvent{TextDeltaEvent{.text = "done"}});
  }

  [[nodiscard]] bool SawExpectedCatalogTool() const {
    return saw_expected_catalog_tool_;
  }

 private:
  std::string tool_name_;
  std::string arguments_json_;
  std::string expected_catalog_tool_;
  int request_count_ = 0;
  bool saw_expected_catalog_tool_ = false;
};

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
                  tool_approval.Resolve(approval->approval_id, true);
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
  ChatServiceToolApproval tool_approval;
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
  auto provider = std::make_shared<TwoRoundToolProvider>(
      "mcp_alpha__tool_a", R"({"value":1})", "mcp_alpha__tool_a");
  MockMcpManager mock_mcp_manager;
  mock_mcp_manager.AddTool("alpha", "tool_a");
  CountingBuiltInExecutor built_in_executor;
  PromptProcessorHarness harness(provider, &mock_mcp_manager,
                                 &built_in_executor);

  harness.processor.ProcessPrompt(1, "run mcp tool", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(provider->SawExpectedCatalogTool());
  CHECK(mock_mcp_manager.invoke_count == 1);
  CHECK(built_in_executor.execute_count == 0);
}

TEST_CASE("builtin_unchanged") {
  auto provider = std::make_shared<TwoRoundToolProvider>(
      "bash", R"({"command":"pwd"})", "bash");
  MockMcpManager mock_mcp_manager;
  mock_mcp_manager.AddTool("alpha", "tool_a");
  CountingBuiltInExecutor built_in_executor;
  PromptProcessorHarness harness(provider, &mock_mcp_manager,
                                 &built_in_executor, true);

  harness.processor.ProcessPrompt(1, "run bash tool", 1,
                                  std::stop_source{}.get_token());

  REQUIRE(provider->SawExpectedCatalogTool());
  CHECK(built_in_executor.execute_count == 1);
  CHECK(mock_mcp_manager.invoke_count == 0);
}

TEST_CASE("approval_from_snapshot") {
  auto provider = std::make_shared<TwoRoundToolProvider>(
      "mcp_alpha__tool_a", R"({"value":2})", "mcp_alpha__tool_a");
  MockMcpManager mock_mcp_manager;
  mock_mcp_manager.AddTool("alpha", "tool_a", true);
  CountingBuiltInExecutor built_in_executor;
  PromptProcessorHarness harness(provider, &mock_mcp_manager,
                                 &built_in_executor, true);

  harness.processor.ProcessPrompt(1, "run approved mcp tool", 1,
                                  std::stop_source{}.get_token());

  const auto approval_it = std::find_if(
      harness.events.begin(), harness.events.end(), [](const ChatEvent& event) {
        return event.Type() == ChatEventType::ToolApprovalRequested;
      });
  REQUIRE(approval_it != harness.events.end());
  CHECK(approval_it->Get<ToolApprovalRequestedEvent>().tool_name ==
        "mcp_alpha__tool_a");
  CHECK(mock_mcp_manager.invoke_count == 1);
  CHECK(built_in_executor.execute_count == 0);
}
