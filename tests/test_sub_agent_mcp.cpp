#include "chat/chat_service_tool_approval.hpp"
#include "chat/sub_agent_manager.hpp"
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

namespace {

class McpToolRequestProvider : public LanguageModelProvider {
 public:
  explicit McpToolRequestProvider(std::string mcp_tool_name)
      : mcp_tool_name_(std::move(mcp_tool_name)) {}

  [[nodiscard]] std::string Id() const override { return "mcp-request-mock"; }

  void CompleteStream(const ChatRequest& request, ChatEventSink sink,
                      std::stop_token stop) override {
    if (stop.stop_requested()) {
      return;
    }
    const bool has_tool_result = std::any_of(
        request.messages.begin(), request.messages.end(),
        [](const ChatMessage& msg) { return msg.role == ChatRole::Tool; });
    if (!has_tool_result) {
      saw_mcp_tool_in_catalog_ =
          std::any_of(request.tools.begin(), request.tools.end(),
                      [this](const ToolDefinition& def) {
                        return def.name == mcp_tool_name_;
                      });
      sink(ChatEvent{
          ToolCallRequestedEvent{.tool_calls = {ToolCallRequest{
                                     .id = "mcp-call-1",
                                     .name = mcp_tool_name_,
                                     .arguments_json = R"({"q":"test"})"}}}});
      return;
    }
    sink(ChatEvent{TextDeltaEvent{.text = "mcp_done"}});
  }

  [[nodiscard]] bool SawMcpToolInCatalog() const {
    return saw_mcp_tool_in_catalog_;
  }

 private:
  std::string mcp_tool_name_;
  bool saw_mcp_tool_in_catalog_ = false;
};

class AuthTrackingMock : public MockMcpManager {
 public:
  int authenticate_count = 0;
};

struct SubAgentMcpTestContext {
  std::filesystem::path workspace;
  ProviderRegistry registry;
  TodoState todo_state;
  std::shared_ptr<ToolExecutor> executor;
  internal::ChatServiceToolApproval tool_approval;
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
          std::lock_guard lock(events_mutex);
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

  std::string SpawnFg(const std::string& task) {
    return manager->SpawnForeground(task, 1, "tc-fg");
  }
};

}  // namespace

TEST_CASE("subagent_invokes_mcp") {
  MockMcpManager mock_manager;
  mock_manager.AddTool("srv1", "search");

  auto provider = std::make_shared<McpToolRequestProvider>("mcp_srv1__search");
  SubAgentMcpTestContext ctx(provider, &mock_manager);

  const auto result = ctx.SpawnFg("run mcp search");

  CHECK(provider->SawMcpToolInCatalog());
  CHECK(mock_manager.invoke_count == 1);
  CHECK(result == "mcp_done");
}

TEST_CASE("subagent_cannot_auth") {
  AuthTrackingMock auth_mock;
  auth_mock.AddTool("auth_srv", "secure_op");
  auth_mock.SetInvokeResult(R"({"error":"Authentication required"})");

  auto provider =
      std::make_shared<McpToolRequestProvider>("mcp_auth_srv__secure_op");
  SubAgentMcpTestContext ctx(provider, &auth_mock);

  const auto result = ctx.SpawnFg("run secure op");

  CHECK(auth_mock.invoke_count == 1);
  CHECK(auth_mock.authenticate_count == 0);
  CHECK(result == "mcp_done");
}
