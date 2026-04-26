#include "mock_mcp_transport.hpp"

#include <chrono>
#include <stop_token>

#include <catch2/catch_test_macros.hpp>

using namespace yac::mcp;
using namespace yac::mcp::test;

TEST_CASE("canned_response") {
  MockMcpTransport transport;
  Json expected = {{"tools", Json::array()}};
  transport.AddCannedResponse("tools/list", expected);

  Json result = transport.SendRequest(
      "tools/list", {}, std::chrono::milliseconds{100}, std::stop_token{});
  REQUIRE(result == expected);
}

TEST_CASE("records_requests") {
  MockMcpTransport transport;
  transport.AddCannedResponse("tools/list", {});
  transport.AddCannedResponse("tools/call", {});
  transport.AddCannedResponse("ping", {});

  (void)transport.SendRequest("tools/list", {{"cursor", ""}},
                              std::chrono::milliseconds{100},
                              std::stop_token{});
  (void)transport.SendRequest("tools/call", {{"name", "bash"}},
                              std::chrono::milliseconds{100},
                              std::stop_token{});
  (void)transport.SendRequest("ping", {}, std::chrono::milliseconds{100},
                              std::stop_token{});

  const auto& reqs = transport.RecordedRequests();
  REQUIRE(reqs.size() == 3);
  REQUIRE(reqs[0].method == "tools/list");
  REQUIRE(reqs[1].method == "tools/call");
  REQUIRE(reqs[2].method == "ping");
}
