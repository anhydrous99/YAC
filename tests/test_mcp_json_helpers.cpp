#include "mcp/json_helpers.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using yac::mcp::Json;
using yac::mcp::McpProtocolError;
using yac::mcp::ParseJsonOrThrow;

TEST_CASE("ParseJsonOrThrow returns parsed JSON on valid input") {
  auto value = ParseJsonOrThrow(R"({"a":1,"b":"two"})", "test context");
  REQUIRE(value.is_object());
  REQUIRE(value["a"] == 1);
  REQUIRE(value["b"] == "two");
}

TEST_CASE("ParseJsonOrThrow handles JSON arrays and primitives") {
  REQUIRE(ParseJsonOrThrow("[1,2,3]", "arr").is_array());
  REQUIRE(ParseJsonOrThrow("42", "num").get<int>() == 42);
  REQUIRE(ParseJsonOrThrow("\"hi\"", "str").get<std::string>() == "hi");
  REQUIRE(ParseJsonOrThrow("null", "null").is_null());
}

TEST_CASE("ParseJsonOrThrow throws McpProtocolError with context prefix") {
  REQUIRE_THROWS_AS(ParseJsonOrThrow("not json", "OAuth token response"),
                    McpProtocolError);
  try {
    (void)ParseJsonOrThrow("not json", "OAuth token response");
    FAIL("expected McpProtocolError");
  } catch (const McpProtocolError& e) {
    using Catch::Matchers::ContainsSubstring;
    REQUIRE_THAT(std::string(e.what()),
                 ContainsSubstring("OAuth token response"));
    REQUIRE_THAT(std::string(e.what()), ContainsSubstring("Invalid JSON"));
  }
}

TEST_CASE("ParseJsonOrThrow throws McpProtocolError on empty input") {
  REQUIRE_THROWS_AS(ParseJsonOrThrow("", "ctx"), McpProtocolError);
}
