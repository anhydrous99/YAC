#include "chat/chat_service.hpp"
#include "chat/chat_service_mcp.hpp"
#include "chat/types.hpp"
#include "core_types/mcp_manager_interface.hpp"
#include "core_types/mcp_resource_types.hpp"
#include "core_types/mcp_tool_catalog_snapshot.hpp"
#include "core_types/tool_call_types.hpp"
#include "mock_mcp_manager.hpp"
#include "provider/provider_registry.hpp"

#include <memory>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::test::MockMcpManager;

namespace {

class TestEmittingMcpManager : public yac::core_types::IMcpManager {
 public:
  yac::chat::ChatEventCallback emit_fn;

  void TriggerStateChange(std::string server_id) const {
    if (emit_fn) {
      emit_fn(yac::chat::MakeMcpServerStateChangedEvent(std::move(server_id),
                                                        "connected", ""));
    }
  }

  [[nodiscard]] yac::core_types::McpToolCatalogSnapshot GetToolCatalogSnapshot()
      const override {
    return {};
  }

  yac::core_types::ToolExecutionResult InvokeTool(
      std::string_view /*qualified_name*/, std::string_view /*arguments_json*/,
      std::stop_token /*stop*/) override {
    return {};
  }

  [[nodiscard]] std::vector<yac::core_types::McpServerStatus>
  GetServerStatusSnapshot() const override {
    return {};
  }

  std::vector<yac::core_types::McpResourceDescriptor> ListResources(
      std::string_view /*server_id*/, std::stop_token /*stop*/) override {
    return {};
  }

  yac::core_types::McpResourceContent ReadResource(
      std::string_view /*server_id*/, std::string_view /*uri*/,
      std::stop_token /*stop*/) override {
    return {};
  }
};

}  // namespace

TEST_CASE("snapshot_through_bootstrap") {
  auto mock = std::make_unique<MockMcpManager>();
  mock->AddTool("server1", "tool_a");

  yac::provider::ProviderRegistry registry;
  yac::chat::ChatService service(std::move(registry), {}, std::move(mock));

  auto* helper = service.GetMcpHelper();
  REQUIRE(helper != nullptr);

  const auto snapshot = helper->BuildToolCatalogSnapshot();

  REQUIRE(snapshot.tools.size() == 1);
  CHECK(snapshot.tools[0].name.find("server1") != std::string::npos);
  CHECK(snapshot.tools[0].name.find("tool_a") != std::string::npos);
}

TEST_CASE("events_propagate") {
  int bridge_invocations = 0;

  auto mock = std::make_unique<TestEmittingMcpManager>();
  TestEmittingMcpManager* trigger = mock.get();
  mock->emit_fn = [&bridge_invocations](yac::chat::ChatEvent event) {
    if (event.Type() == yac::chat::ChatEventType::McpServerStateChanged) {
      ++bridge_invocations;
    }
  };

  yac::provider::ProviderRegistry registry;
  yac::chat::ChatService service(std::move(registry), {}, std::move(mock));

  trigger->TriggerStateChange("test-server");

  REQUIRE(bridge_invocations == 1);
}
