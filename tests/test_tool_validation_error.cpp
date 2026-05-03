#include "tool_call/executor.hpp"
#include "tool_call/tool_validation_error.hpp"

#include <openai.hpp>
#include <string>

#include <catch2/catch_test_macros.hpp>

using yac::tool_call::BuildValidationErrorJson;
using yac::tool_call::ToolValidationError;
using Json = openai::_detail::Json;

TEST_CASE("ToolValidationError carries tool name and raw arguments") {
  ToolValidationError err("Missing string argument 'filepath'.", "file_edit",
                          R"({"old_string":"x"})");
  REQUIRE(std::string(err.what()) == "Missing string argument 'filepath'.");
  REQUIRE(err.tool_name() == "file_edit");
  REQUIRE(err.raw_arguments_json() == R"({"old_string":"x"})");
}

TEST_CASE("BuildValidationErrorJson includes parsed schema for known tool") {
  ToolValidationError err("Missing string argument 'filepath'.", "file_edit",
                          R"({"old_string":"x"})");
  const auto definitions = yac::tool_call::ToolExecutor::Definitions();
  const auto json = Json::parse(BuildValidationErrorJson(err, definitions));

  REQUIRE(json["error"] == "Missing string argument 'filepath'.");
  REQUIRE(json["tool_name"] == "file_edit");
  REQUIRE(json["received_arguments"] == R"({"old_string":"x"})");
  REQUIRE(json.contains("expected_schema"));
  REQUIRE(json["expected_schema"].is_object());

  const auto& required = json["expected_schema"]["required"];
  REQUIRE(required.is_array());
  bool has_filepath = false;
  bool has_old_string = false;
  bool has_new_string = false;
  for (const auto& item : required) {
    if (item == "filepath") has_filepath = true;
    if (item == "old_string") has_old_string = true;
    if (item == "new_string") has_new_string = true;
  }
  REQUIRE(has_filepath);
  REQUIRE(has_old_string);
  REQUIRE(has_new_string);
}

TEST_CASE("BuildValidationErrorJson omits schema for unknown tool") {
  ToolValidationError err("Unknown tool: not_a_real_tool.", "not_a_real_tool",
                          "{}");
  const auto definitions = yac::tool_call::ToolExecutor::Definitions();
  const auto json = Json::parse(BuildValidationErrorJson(err, definitions));

  REQUIRE(json["error"] == "Unknown tool: not_a_real_tool.");
  REQUIRE(json["tool_name"] == "not_a_real_tool");
  REQUIRE(json["received_arguments"] == "{}");
  REQUIRE_FALSE(json.contains("expected_schema"));
}

TEST_CASE("BuildValidationErrorJson handles empty raw arguments") {
  ToolValidationError err("Missing string argument 'filepath'.", "file_edit",
                          "");
  const auto definitions = yac::tool_call::ToolExecutor::Definitions();
  const auto json = Json::parse(BuildValidationErrorJson(err, definitions));
  REQUIRE(json["received_arguments"] == "");
  REQUIRE(json.contains("expected_schema"));
}

TEST_CASE("BuildValidationErrorJson string overload omits schema") {
  const auto json = Json::parse(BuildValidationErrorJson(
      "Some MCP transport error", "mcp_alpha__tool_a", R"({"value":1})"));
  REQUIRE(json["error"] == "Some MCP transport error");
  REQUIRE(json["tool_name"] == "mcp_alpha__tool_a");
  REQUIRE(json["received_arguments"] == R"({"value":1})");
  REQUIRE_FALSE(json.contains("expected_schema"));
}

TEST_CASE("BuildValidationErrorJson string overload accepts empty tool name") {
  const auto json =
      Json::parse(BuildValidationErrorJson("boom", "", "raw bytes"));
  REQUIRE(json["error"] == "boom");
  REQUIRE_FALSE(json.contains("tool_name"));
  REQUIRE(json["received_arguments"] == "raw bytes");
}
