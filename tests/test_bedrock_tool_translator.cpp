#include "provider/bedrock_chat_protocol.hpp"

#include <aws/bedrock-runtime/model/Tool.h>
#include <aws/bedrock-runtime/model/ToolResultContentBlock.h>
#include <aws/bedrock-runtime/model/ToolSpecification.h>
#include <stdexcept>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::provider;

namespace {

constexpr const char* kSimpleSchema =
    R"({"type":"object","properties":{"path":{"type":"string"}}})";

}  // namespace

TEST_CASE("TranslateToolDefinitions: empty vector yields no tools") {
  std::vector<ToolDefinition> tools;
  auto result = TranslateToolDefinitions(tools);
  REQUIRE(result.config.GetTools().empty());
}

TEST_CASE("TranslateToolDefinitions: single tool name is preserved") {
  std::vector<ToolDefinition> tools = {
      {.name = "file_read",
       .description = "Read a file",
       .parameters_schema_json = kSimpleSchema}};
  auto result = TranslateToolDefinitions(tools);
  REQUIRE(result.config.GetTools().size() == 1);
  const auto& spec = result.config.GetTools()[0].GetToolSpec();
  REQUIRE(spec.GetName() == "file_read");
}

TEST_CASE("TranslateToolDefinitions: tool description is preserved") {
  std::vector<ToolDefinition> tools = {
      {.name = "list_dir",
       .description = "List directory contents",
       .parameters_schema_json = kSimpleSchema}};
  auto result = TranslateToolDefinitions(tools);
  REQUIRE(result.config.GetTools().size() == 1);
  const auto& spec = result.config.GetTools()[0].GetToolSpec();
  REQUIRE(spec.GetDescription() == "List directory contents");
}

TEST_CASE("TranslateToolDefinitions: multiple tools yields correct count") {
  std::vector<ToolDefinition> tools = {
      {.name = "tool_a",
       .description = "a",
       .parameters_schema_json = R"({"type":"object"})"},
      {.name = "tool_b",
       .description = "b",
       .parameters_schema_json = R"({"type":"object"})"},
      {.name = "tool_c",
       .description = "c",
       .parameters_schema_json = R"({"type":"object"})"},
  };
  auto result = TranslateToolDefinitions(tools);
  REQUIRE(result.config.GetTools().size() == 3);
}

TEST_CASE("TranslateToolDefinitions: multiple tools preserve all names") {
  std::vector<ToolDefinition> tools = {
      {.name = "grep",
       .description = "search",
       .parameters_schema_json = R"({"type":"object"})"},
      {.name = "bash",
       .description = "shell",
       .parameters_schema_json = R"({"type":"object"})"},
  };
  auto result = TranslateToolDefinitions(tools);
  REQUIRE(result.config.GetTools()[0].GetToolSpec().GetName() == "grep");
  REQUIRE(result.config.GetTools()[1].GetToolSpec().GetName() == "bash");
}

TEST_CASE("TranslateToolDefinitions: invalid JSON schema throws") {
  std::vector<ToolDefinition> tools = {
      {.name = "bad_tool",
       .description = "test",
       .parameters_schema_json = "not valid json"}};
  REQUIRE_THROWS_AS(TranslateToolDefinitions(tools), std::runtime_error);
}

TEST_CASE("TranslateToolDefinitions: tool name with '.' rejected") {
  std::vector<ToolDefinition> tools = {
      {.name = "namespace.tool",
       .description = "x",
       .parameters_schema_json = kSimpleSchema}};
  try {
    TranslateToolDefinitions(tools);
    FAIL("Expected std::runtime_error for invalid Bedrock tool name");
  } catch (const std::runtime_error& err) {
    const std::string msg = err.what();
    REQUIRE(msg.find("[bedrock-validation]") != std::string::npos);
    REQUIRE(msg.find("namespace.tool") != std::string::npos);
  }
}

TEST_CASE("TranslateToolDefinitions: tool name with ':' rejected") {
  std::vector<ToolDefinition> tools = {
      {.name = "tool:bad",
       .description = "x",
       .parameters_schema_json = kSimpleSchema}};
  REQUIRE_THROWS_AS(TranslateToolDefinitions(tools), std::runtime_error);
}

TEST_CASE("TranslateToolDefinitions: tool name with whitespace rejected") {
  std::vector<ToolDefinition> tools = {
      {.name = "bad name",
       .description = "x",
       .parameters_schema_json = kSimpleSchema}};
  REQUIRE_THROWS_AS(TranslateToolDefinitions(tools), std::runtime_error);
}

TEST_CASE("TranslateToolDefinitions: tool name >64 chars rejected") {
  std::string too_long(65, 'a');
  std::vector<ToolDefinition> tools = {
      {.name = too_long,
       .description = "x",
       .parameters_schema_json = kSimpleSchema}};
  try {
    TranslateToolDefinitions(tools);
    FAIL("Expected std::runtime_error for too-long tool name");
  } catch (const std::runtime_error& err) {
    const std::string msg = err.what();
    REQUIRE(msg.find("[bedrock-validation]") != std::string::npos);
  }
}

TEST_CASE("TranslateToolDefinitions: tool name exactly 64 chars accepted") {
  std::string boundary(64, 'a');
  std::vector<ToolDefinition> tools = {
      {.name = boundary,
       .description = "x",
       .parameters_schema_json = kSimpleSchema}};
  REQUIRE_NOTHROW(TranslateToolDefinitions(tools));
}

TEST_CASE(
    "TranslateToolDefinitions: tool name with hyphen and underscore accepted") {
  std::vector<ToolDefinition> tools = {
      {.name = "tool_name-v2",
       .description = "x",
       .parameters_schema_json = kSimpleSchema}};
  REQUIRE_NOTHROW(TranslateToolDefinitions(tools));
}

TEST_CASE("TranslateToolDefinitions: error message contains the tool name") {
  std::vector<ToolDefinition> tools = {{.name = "my_broken_tool",
                                        .description = "test",
                                        .parameters_schema_json = "{invalid"}};
  try {
    TranslateToolDefinitions(tools);
    FAIL("Expected std::runtime_error to be thrown");
  } catch (const std::runtime_error& err) {
    const std::string msg = err.what();
    REQUIRE(msg.find("my_broken_tool") != std::string::npos);
  }
}

TEST_CASE("TranslateToolUseToYac: id is passed through") {
  auto req = TranslateToolUseToYac("call-xyz", "tool_name", "{}");
  REQUIRE(req.id == "call-xyz");
}

TEST_CASE("TranslateToolUseToYac: name is passed through") {
  auto req = TranslateToolUseToYac("call-1", "file_read", "{}");
  REQUIRE(req.name == "file_read");
}

TEST_CASE("TranslateToolUseToYac: arguments_json is passed through") {
  const std::string args = R"({"path":"src/main.cpp"})";
  auto req = TranslateToolUseToYac("call-2", "file_read", args);
  REQUIRE(req.arguments_json == args);
}

TEST_CASE("TranslateToolUseToYac: all fields are mapped correctly") {
  auto req = TranslateToolUseToYac("call-abc", "grep", R"({"pattern":"TODO"})");
  REQUIRE(req.id == "call-abc");
  REQUIRE(req.name == "grep");
  REQUIRE(req.arguments_json == R"({"pattern":"TODO"})");
}

TEST_CASE("TranslateToolUseToYac: empty arguments_json is preserved") {
  auto req = TranslateToolUseToYac("call-empty", "list_dir", "");
  REQUIRE(req.id == "call-empty");
  REQUIRE(req.name == "list_dir");
  REQUIRE(req.arguments_json.empty());
}

TEST_CASE("TranslateYacToolResultToBedrock: tool_call_id maps to ToolUseId") {
  ChatMessage msg;
  msg.role = ChatRole::Tool;
  msg.content = "output";
  msg.tool_call_id = yac::ToolCallId{"call-result-1"};
  auto result = TranslateYacToolResultToBedrock(msg);
  REQUIRE(result.block.GetToolUseId() == "call-result-1");
}

TEST_CASE("TranslateYacToolResultToBedrock: content maps to text block") {
  ChatMessage msg;
  msg.role = ChatRole::Tool;
  msg.content = "file contents here";
  msg.tool_call_id = yac::ToolCallId{"call-result-2"};
  auto result = TranslateYacToolResultToBedrock(msg);
  REQUIRE(result.block.GetContent().size() == 1);
  REQUIRE(result.block.GetContent()[0].GetText() == "file contents here");
}

TEST_CASE("TranslateYacToolResultToBedrock: empty content is preserved") {
  ChatMessage msg;
  msg.role = ChatRole::Tool;
  msg.content = "";
  msg.tool_call_id = yac::ToolCallId{"call-empty-result"};
  auto result = TranslateYacToolResultToBedrock(msg);
  REQUIRE(result.block.GetContent().size() == 1);
  REQUIRE(result.block.GetContent()[0].GetText().empty());
}

TEST_CASE("TranslateYacToolResultToBedrock: all fields mapped in one call") {
  ChatMessage msg;
  msg.role = ChatRole::Tool;
  msg.content = "tool output";
  msg.tool_call_id = yac::ToolCallId{"call-full-result"};
  auto result = TranslateYacToolResultToBedrock(msg);
  REQUIRE(result.block.GetToolUseId() == "call-full-result");
  REQUIRE(result.block.GetContent().size() == 1);
  REQUIRE(result.block.GetContent()[0].GetText() == "tool output");
}
