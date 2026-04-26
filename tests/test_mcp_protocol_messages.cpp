#include "mcp/protocol_constants.hpp"
#include "mcp/protocol_messages.hpp"

#include <optional>
#include <string>
#include <variant>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("initialize_round_trip") {
  namespace mcp = yac::mcp;
  namespace pc = yac::mcp::protocol;

  const auto canonical = mcp::Json::parse(R"({
    "protocolVersion": "2025-11-25",
    "capabilities": {
      "roots": {"listChanged": true},
      "sampling": {}
    },
    "clientInfo": {
      "name": "YAC",
      "version": "0.1.0"
    }
  })");

  const auto req = mcp::InitializeRequest::FromJson(canonical);
  REQUIRE(req.protocol_version == std::string(pc::kMcpProtocolVersion));
  REQUIRE(req.capabilities.has_roots);
  REQUIRE(req.capabilities.roots_list_changed);
  REQUIRE(req.capabilities.has_sampling);
  REQUIRE(req.client_info.name == "YAC");
  REQUIRE(req.client_info.version == "0.1.0");

  const auto round_tripped = req.ToJson();
  REQUIRE(round_tripped == canonical);
}

TEST_CASE("tools_call_mixed_content") {
  namespace mcp = yac::mcp;

  const auto json = mcp::Json::parse(R"({
    "content": [
      {"type": "text", "text": "Hello, World!"},
      {"type": "image", "data": "iVBORw0KGgo=", "mimeType": "image/png"},
      {"type": "resource_link", "uri": "file:///example.txt", "name": "example"}
    ],
    "isError": false
  })");

  const auto resp = mcp::ToolsCallResponse::FromJson(json);
  REQUIRE(resp.result_blocks.size() == 3);
  REQUIRE(!resp.is_error);

  REQUIRE(std::holds_alternative<mcp::TextContent>(resp.result_blocks[0]));
  const auto& text = std::get<mcp::TextContent>(resp.result_blocks[0]);
  REQUIRE(text.text == "Hello, World!");

  REQUIRE(std::holds_alternative<mcp::ImageContent>(resp.result_blocks[1]));
  const auto& image = std::get<mcp::ImageContent>(resp.result_blocks[1]);
  REQUIRE(image.mime_type == "image/png");

  const auto& block2 = resp.result_blocks[2];
  REQUIRE(std::holds_alternative<mcp::ResourceLinkContent>(block2));
  const auto& link = std::get<mcp::ResourceLinkContent>(block2);
  REQUIRE(link.uri == "file:///example.txt");
}

TEST_CASE("cancelled_notification_round_trip") {
  namespace mcp = yac::mcp;

  const auto canonical = mcp::Json::parse(R"({
    "requestId": 42,
    "reason": "user cancelled"
  })");

  const auto notif = mcp::CancelledNotification::FromJson(canonical);
  REQUIRE(notif.request_id == mcp::Json(42));
  REQUIRE(notif.reason == std::optional<std::string>{"user cancelled"});

  const auto round_tripped = notif.ToJson();
  REQUIRE(round_tripped == canonical);
}
