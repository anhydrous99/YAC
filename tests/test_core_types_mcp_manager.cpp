#include "core_types/mcp_manager_interface.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::core_types;

TEST_CASE("imcp_manager_compiles") {
  McpToolApprovalPolicy policy;
  REQUIRE_FALSE(policy.requires_approval);

  McpToolCatalogSnapshot snapshot;
  REQUIRE(snapshot.revision_id == 0);
  REQUIRE(snapshot.tools.empty());
  REQUIRE(snapshot.name_to_server_tool.empty());
  REQUIRE(snapshot.approval_policy.empty());

  McpResourceDescriptor descriptor;
  REQUIRE(descriptor.uri.empty());
  REQUIRE(descriptor.size == 0);

  McpResourceContent content;
  REQUIRE(content.uri.empty());
  REQUIRE(content.blob.empty());
  REQUIRE_FALSE(content.is_truncated);
  REQUIRE(content.total_bytes == 0);

  McpServerStatus status;
  REQUIRE(status.id.empty());
  REQUIRE(status.tool_count == 0);
  REQUIRE(status.resource_count == 0);
}

TEST_CASE("no_leaky_includes") {
  McpToolCatalogSnapshot snapshot;
  REQUIRE(snapshot.tools.empty());
  REQUIRE(snapshot.approval_policy.empty());
}
