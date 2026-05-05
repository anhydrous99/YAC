#include "mcp/tool_naming.hpp"

#include <random>
#include <regex>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::Matches;

namespace {

constexpr std::size_t kBedrockMaxToolNameLength = 64;
constexpr std::string_view kBedrockToolNameRegex = "^[a-zA-Z0-9_-]+$";

void AssertBedrockInvariant(const std::string& result) {
  REQUIRE(result.size() <= kBedrockMaxToolNameLength);
  REQUIRE_THAT(result, Matches(std::string(kBedrockToolNameRegex)));
  REQUIRE(result.size() > 0);
}

}  // namespace

TEST_CASE("bedrock_invariant_randomized_50_combinations",
          "[mcp_tool_name_invariant]") {
  std::mt19937 rng{42};
  std::uniform_int_distribution<std::size_t> len_dist(1, 100);
  std::uniform_int_distribution<unsigned char> char_dist(32, 126);

  for (int i = 0; i < 50; ++i) {
    std::string server_id;
    std::string tool_name;

    std::size_t server_len = len_dist(rng);
    for (std::size_t j = 0; j < server_len; ++j) {
      server_id.push_back(static_cast<char>(char_dist(rng)));
    }

    std::size_t tool_len = len_dist(rng);
    for (std::size_t j = 0; j < tool_len; ++j) {
      tool_name.push_back(static_cast<char>(char_dist(rng)));
    }

    bool has_valid_char = false;
    for (unsigned char ch : server_id) {
      if (std::isalnum(ch) != 0 || ch == '-' || ch == '_') {
        has_valid_char = true;
        break;
      }
    }
    if (!has_valid_char) {
      continue;
    }

    const std::string result =
        yac::mcp::SanitizeMcpToolName(server_id, tool_name);
    AssertBedrockInvariant(result);
  }
}

TEST_CASE("bedrock_invariant_server_id_100_chars",
          "[mcp_tool_name_invariant]") {
  const std::string server_id(100, 'a');
  const std::string tool_name = "test_tool";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_tool_name_100_chars",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "github";
  const std::string tool_name(100, 'x');
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_both_100_chars", "[mcp_tool_name_invariant]") {
  const std::string server_id(100, 'a');
  const std::string tool_name(100, 'x');
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_utf8_emoji_chars", "[mcp_tool_name_invariant]") {
  const std::string server_id = "server_🚀_id";
  const std::string tool_name = "tool_🎯_name";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_shell_metacharacters",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "server;id|with$meta";
  const std::string tool_name = "tool;name|with$meta";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_determinism", "[mcp_tool_name_invariant]") {
  const std::string server_id = "test.server/id";
  const std::string tool_name = "test.tool/name";

  const std::string result1 =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  const std::string result2 =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);

  REQUIRE(result1 == result2);
  AssertBedrockInvariant(result1);
}

TEST_CASE("bedrock_invariant_empty_tool_name", "[mcp_tool_name_invariant]") {
  const std::string server_id = "github";
  const std::string tool_name = "";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_empty_server_id_throws",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "";
  const std::string tool_name = "test_tool";
  REQUIRE_THROWS_AS(yac::mcp::SanitizeMcpToolName(server_id, tool_name),
                    std::invalid_argument);
}

TEST_CASE("bedrock_invariant_special_chars_only_server",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "!!!@@@###";
  const std::string tool_name = "valid_tool";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_mixed_case_alphanumeric",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "MyServer123";
  const std::string tool_name = "MyTool456";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_dashes_and_underscores",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "my-server_id";
  const std::string tool_name = "my-tool_name";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
  REQUIRE(result == "mcp_my-server_id__my-tool_name");
}

TEST_CASE("bedrock_invariant_whitespace_chars", "[mcp_tool_name_invariant]") {
  const std::string server_id = "server id\twith\nwhitespace";
  const std::string tool_name = "tool name\twith\nwhitespace";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_single_char_server", "[mcp_tool_name_invariant]") {
  const std::string server_id = "a";
  const std::string tool_name = "b";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
  REQUIRE(result == "mcp_a__b");
}

TEST_CASE("bedrock_invariant_max_length_boundary",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "a";
  const std::string tool_name(100, 'x');
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
  REQUIRE(result.size() <= 64);
}

TEST_CASE("bedrock_invariant_numeric_only", "[mcp_tool_name_invariant]") {
  const std::string server_id = "123456";
  const std::string tool_name = "789012";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_leading_trailing_special",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "...server...";
  const std::string tool_name = "...tool...";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_consecutive_special_chars",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "server@@@id";
  const std::string tool_name = "tool###name";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_long_server_short_tool",
          "[mcp_tool_name_invariant]") {
  const std::string server_id(50, 'a');
  const std::string tool_name = "x";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_short_server_long_tool",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "a";
  const std::string tool_name(100, 'x');
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_hash_suffix_present",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "github";
  const std::string tool_name(100, 'x');
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
  REQUIRE(result.find("_h") != std::string::npos);
}

TEST_CASE("bedrock_invariant_prefix_always_present",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "test";
  const std::string tool_name = "tool";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  REQUIRE(result.starts_with("mcp_"));
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_separator_always_present",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "test";
  const std::string tool_name = "tool";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  REQUIRE(result.find("__") != std::string::npos);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_no_consecutive_underscores_except_separator",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "server__id";
  const std::string tool_name = "tool__name";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
}

TEST_CASE("bedrock_invariant_all_valid_chars_preserved",
          "[mcp_tool_name_invariant]") {
  const std::string server_id = "server-id_123";
  const std::string tool_name = "tool-name_456";
  const std::string result =
      yac::mcp::SanitizeMcpToolName(server_id, tool_name);
  AssertBedrockInvariant(result);
  REQUIRE(result.find("server-id_123") != std::string::npos);
  REQUIRE(result.find("tool-name_456") != std::string::npos);
}

TEST_CASE("bedrock_invariant_stress_test_100_random_pairs",
          "[mcp_tool_name_invariant]") {
  std::mt19937 rng{12345};
  std::uniform_int_distribution<std::size_t> len_dist(1, 80);
  std::uniform_int_distribution<unsigned char> char_dist(33, 126);

  for (int i = 0; i < 100; ++i) {
    std::string server_id;
    std::string tool_name;

    std::size_t server_len = len_dist(rng);
    for (std::size_t j = 0; j < server_len; ++j) {
      server_id.push_back(static_cast<char>(char_dist(rng)));
    }

    std::size_t tool_len = len_dist(rng);
    for (std::size_t j = 0; j < tool_len; ++j) {
      tool_name.push_back(static_cast<char>(char_dist(rng)));
    }

    bool has_valid_char = false;
    for (unsigned char ch : server_id) {
      if (std::isalnum(ch) != 0 || ch == '-' || ch == '_') {
        has_valid_char = true;
        break;
      }
    }
    if (!has_valid_char) {
      continue;
    }

    const std::string result =
        yac::mcp::SanitizeMcpToolName(server_id, tool_name);
    AssertBedrockInvariant(result);
  }
}
