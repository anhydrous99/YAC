#include "mcp/tool_naming.hpp"

#include <stdexcept>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::Matches;

namespace {

constexpr const char* kRegex = "^[a-zA-Z0-9_-]{1,64}$";

}  // namespace

TEST_CASE("standard", "[mcp_tool_naming]") {
  REQUIRE(yac::mcp::SanitizeMcpToolName("github", "issues_list") ==
          "mcp_github__issues_list");
}

TEST_CASE("special_chars", "[mcp_tool_naming]") {
  const std::string qualified = yac::mcp::SanitizeMcpToolName("a.b/c", "x.y/z");
  REQUIRE(qualified == "mcp_a_b_c__x_y_z");
  REQUIRE_THAT(qualified, Matches(kRegex));
}

TEST_CASE("length_cap_server", "[mcp_tool_naming]") {
  const std::string qualified =
      yac::mcp::SanitizeMcpToolName("abcdefghijklmnopqrs", "tool");
  REQUIRE(qualified == "mcp_abcdefghijklmnop__tool");
  REQUIRE(qualified.size() <= 64);
}

TEST_CASE("length_cap_tool", "[mcp_tool_naming]") {
  const std::string qualified = yac::mcp::SanitizeMcpToolName(
      "github", "abcdefghijklmnopqrstuvwxyz0123456789_extra");
  REQUIRE(qualified.starts_with("mcp_github__"));
  REQUIRE(qualified.size() == 4 + 6 + 2 + 41);
  REQUIRE(qualified.find("_h") != std::string::npos);
  REQUIRE_THAT(qualified, Matches(kRegex));
}

TEST_CASE("length_cap_both", "[mcp_tool_naming]") {
  const std::string qualified = yac::mcp::SanitizeMcpToolName(
      "server_id_with_extra_chars", "tool_name_with_extra_chars_and_more_data");
  REQUIRE(qualified.size() <= 64);
  REQUIRE_THAT(qualified, Matches(kRegex));
}

TEST_CASE("rejects_empty_server", "[mcp_tool_naming]") {
  REQUIRE_THROWS_AS(yac::mcp::SanitizeMcpToolName("", "x"),
                    std::invalid_argument);
}

TEST_CASE("prefix_collision", "[mcp_tool_naming]") {
  REQUIRE_FALSE(yac::mcp::IsMcpToolName("bash"));
  REQUIRE_FALSE(yac::mcp::IsMcpToolName("mcp_but_missing_separator"));
}

TEST_CASE("round_trip", "[mcp_tool_naming]") {
  const std::string qualified =
      yac::mcp::SanitizeMcpToolName("github", "issues_list");
  const auto split = yac::mcp::SplitMcpToolName(qualified);
  REQUIRE(split.has_value());
  REQUIRE(split->first == "github");
  REQUIRE(split->second == "issues_list");
}

TEST_CASE("regex_compliance", "[mcp_tool_naming]") {
  const std::string qualified = yac::mcp::SanitizeMcpToolName("a.b/c", "x.y/z");
  REQUIRE_THAT(qualified, Matches(kRegex));
}
