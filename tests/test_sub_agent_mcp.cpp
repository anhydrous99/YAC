#include "chat/sub_agent_manager.hpp"
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
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;
using namespace yac::tool_call;
using yac::test::MockMcpManager;
using yac::testing::LambdaMockProvider;

namespace {

std::shared_ptr<LambdaMockProvider> MakeMcpToolRequestProvider(
    std::string mcp_tool_name, std::shared_ptr<bool> saw_mcp_tool_in_catalog) {
  return std::make_shared<LambdaMockProvider>(
      "mcp-request-mock",
      [mcp_tool_name = std::move(mcp_tool_name), saw_mcp_tool_in_catalog](
          const ChatRequest& request, ChatEventSink sink,
          std::stop_token stop) {
        if (stop.stop_requested()) {
          return;
        }
        const bool has_tool_result = std::ranges::any_of(
            request.messages,
            [](const ChatMessage& msg) { return msg.role == ChatRole::Tool; });
        if (!has_tool_result) {
          *saw_mcp_tool_in_catalog = std::ranges::any_of(
              request.tools, [&mcp_tool_name](const ToolDefinition& def) {
                return def.name == mcp_tool_name;
              });
          sink(ChatEvent{ToolCallRequestedEvent{
              .tool_calls = {
                  ToolCallRequest{.id = "mcp-call-1",
                                  .name = mcp_tool_name,
                                  .arguments_json = R"({"q":"test"})"}}}});
          return;
        }
        sink(ChatEvent{TextDeltaEvent{.text = "mcp_done"}});
      });
}

class AuthTrackingMock : public MockMcpManager {
 public:
  int authenticate_count = 0;
};

struct SubAgentMcpTestContext {
  std::filesystem::path workspace;
  ProviderRegistry registry;
  TodoState todo_state;
  std::shared_ptr<ToolExecutor> executor;
  ToolApprovalManager tool_approval;
  std::mutex events_mutex;
  std::vector<ChatEvent> events;
  ChatConfig config;
  std::unique_ptr<SubAgentManager> manager;

  explicit SubAgentMcpTestContext(std::shared_ptr<LanguageModelProvider> prov,
                                  yac::core_types::IMcpManager* mcp_manager) {
    static std::atomic<int> counter{0};
    workspace = std::filesystem::temp_directory_path() /
                ("yac_sam_mcp_test_" + std::to_string(counter.fetch_add(1)));
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
        [this]() { return config; });
    manager->SetMcpManager(mcp_manager);
  }

  ~SubAgentMcpTestContext() {
    manager.reset();
    std::filesystem::remove_all(workspace);
  }

  SubAgentMcpTestContext(const SubAgentMcpTestContext&) = delete;
  SubAgentMcpTestContext& operator=(const SubAgentMcpTestContext&) = delete;
  SubAgentMcpTestContext(SubAgentMcpTestContext&&) = delete;
  SubAgentMcpTestContext& operator=(SubAgentMcpTestContext&&) = delete;

  std::string SpawnFg(const std::string& task) const {
    return manager->SpawnForeground(task, 1, "tc-fg");
  }
};

}  // namespace

TEST_CASE("subagent_invokes_mcp") {
  MockMcpManager mock_manager;
  mock_manager.AddTool("srv1", "search");

  auto saw_mcp_tool = std::make_shared<bool>(false);
  auto provider = MakeMcpToolRequestProvider("mcp_srv1__search", saw_mcp_tool);
  SubAgentMcpTestContext ctx(provider, &mock_manager);

  const auto result = ctx.SpawnFg("run mcp search");

  CHECK(*saw_mcp_tool);
  CHECK(mock_manager.invoke_count == 1);
  CHECK(result == "mcp_done");
}

TEST_CASE("subagent_cannot_auth") {
  AuthTrackingMock auth_mock;
  auth_mock.AddTool("auth_srv", "secure_op");
  auth_mock.SetInvokeResult(R"({"error":"Authentication required"})");

  auto saw_mcp_tool = std::make_shared<bool>(false);
  auto provider =
      MakeMcpToolRequestProvider("mcp_auth_srv__secure_op", saw_mcp_tool);
  SubAgentMcpTestContext ctx(provider, &auth_mock);

  const auto result = ctx.SpawnFg("run secure op");

  CHECK(auth_mock.invoke_count == 1);
  CHECK(auth_mock.authenticate_count == 0);
  CHECK(result == "mcp_done");
}
