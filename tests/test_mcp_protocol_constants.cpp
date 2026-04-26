#include "mcp/protocol_constants.hpp"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("mcp protocol constants expose wire identifiers") {
  using namespace yac::mcp::protocol;

  REQUIRE(kMcpProtocolVersion == "2025-11-25");
  REQUIRE(kMethodInitialize == "initialize");
  REQUIRE(kMethodToolsList == "tools/list");
  REQUIRE(kErrorMethodNotFound == -32601);
}
