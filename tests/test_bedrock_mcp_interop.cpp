#include "mcp/tool_naming.hpp"
#include "provider/bedrock_chat_protocol.hpp"

#include <aws/bedrock-runtime/model/Tool.h>
#include <aws/bedrock-runtime/model/ToolSpecification.h>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace yac::chat;
using namespace yac::provider;
using Catch::Matchers::Matches;

namespace {

// 16-char server ID saturates the server-ID budget (kServerIdMax = 16).
constexpr std::string_view kServer16 = "aaaaaaaaaaaaaaaa";
// 38-char tool name: mcp_(4) + server(16) + __(2) + tool(38) = 60 chars.
constexpr std::string_view kTool38 =
    "bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb";

constexpr std::size_t kExpectedLen = 60;
constexpr std::size_t kBedrockMaxLen = 64;
constexpr std::string_view kBedrockPattern = "^[a-zA-Z0-9_-]+$";
constexpr const char* kSchema =
    R"({"type":"object","properties":{"x":{"type":"string"}}})";

}  // namespace

TEST_CASE("mcp_60char_sanitized_name_is_60_chars",
          "[bedrock_mcp_interop]") {
  const std::string name =
      yac::mcp::SanitizeMcpToolName(kServer16, kTool38);
  REQUIRE(name.size() == kExpectedLen);
}

TEST_CASE("mcp_60char_sanitized_name_within_bedrock_limit",
          "[bedrock_mcp_interop]") {
  const std::string name =
      yac::mcp::SanitizeMcpToolName(kServer16, kTool38);
  REQUIRE(name.size() <= kBedrockMaxLen);
}

TEST_CASE("mcp_60char_sanitized_name_matches_bedrock_regex",
          "[bedrock_mcp_interop]") {
  const std::string name =
      yac::mcp::SanitizeMcpToolName(kServer16, kTool38);
  REQUIRE_THAT(name, Matches(std::string(kBedrockPattern)));
}

TEST_CASE("mcp_60char_tool_in_translate_tool_definitions",
          "[bedrock_mcp_interop]") {
  const std::string name =
      yac::mcp::SanitizeMcpToolName(kServer16, kTool38);
  std::vector<ToolDefinition> tools = {
      {.name = name,
       .description = "60-char MCP tool",
       .parameters_schema_json = kSchema}};
  auto result = TranslateToolDefinitions(tools);
  REQUIRE(result.config.GetTools().size() == 1);
  const auto& spec = result.config.GetTools()[0].GetToolSpec();
  REQUIRE(std::string(spec.GetName().c_str()) == name);
}

TEST_CASE("mcp_60char_tool_spec_name_passes_bedrock_validation",
          "[bedrock_mcp_interop]") {
  const std::string name =
      yac::mcp::SanitizeMcpToolName(kServer16, kTool38);
  std::vector<ToolDefinition> tools = {
      {.name = name,
       .description = "Bedrock validation check",
       .parameters_schema_json = kSchema}};
  auto result = TranslateToolDefinitions(tools);
  const std::string spec_name{
      result.config.GetTools()[0].GetToolSpec().GetName().c_str()};
  REQUIRE(spec_name.size() <= kBedrockMaxLen);
  REQUIRE_THAT(spec_name, Matches(std::string(kBedrockPattern)));
}

TEST_CASE("mcp_60char_tool_use_id_round_trips",
          "[bedrock_mcp_interop]") {
  const std::string name =
      yac::mcp::SanitizeMcpToolName(kServer16, kTool38);
  auto req = TranslateToolUseToYac("call-mcp-001", name, "{}");
  REQUIRE(req.id == "call-mcp-001");
}

TEST_CASE("mcp_60char_tool_use_name_round_trips",
          "[bedrock_mcp_interop]") {
  const std::string name =
      yac::mcp::SanitizeMcpToolName(kServer16, kTool38);
  auto req =
      TranslateToolUseToYac("call-mcp-002", name, R"({"x":"y"})");
  REQUIRE(req.name == name);
}

TEST_CASE("mcp_60char_tool_use_arguments_round_trips",
          "[bedrock_mcp_interop]") {
  const std::string name =
      yac::mcp::SanitizeMcpToolName(kServer16, kTool38);
  const std::string args = R"({"x":"hello"})";
  auto req = TranslateToolUseToYac("call-mcp-003", name, args);
  REQUIRE(req.arguments_json == args);
}
