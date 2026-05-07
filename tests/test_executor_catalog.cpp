#include "core_types/chat_ids.hpp"
#include "tool_call/executor_arguments.hpp"
#include "tool_call/executor_catalog.hpp"
#include "tool_call/tool_validation_error.hpp"

#include <string>
#include <unordered_set>

#include <catch2/catch_test_macros.hpp>

using yac::chat::ToolCallRequest;
using yac::tool_call::Json;
using yac::tool_call::LookupToolHandler;
using yac::tool_call::PrepareToolCall;
using yac::tool_call::ToolDefinitions;
using yac::tool_call::ToolHandlerCount;
using yac::tool_call::ToolValidationError;

namespace {

ToolCallRequest MakeRequest(std::string name, std::string args_json) {
  return ToolCallRequest{.id = "t",
                         .name = std::move(name),
                         .arguments_json = std::move(args_json)};
}

}  // namespace

TEST_CASE("ToolDefinitions: tool names are unique") {
  const auto defs = ToolDefinitions();
  std::unordered_set<std::string> seen;
  for (const auto& def : defs) {
    INFO("Duplicate name: " << def.name);
    CHECK(seen.insert(def.name).second);
  }
}

TEST_CASE("ToolDefinitions: handler count matches definition count") {
  REQUIRE(ToolHandlerCount() == ToolDefinitions().size());
}

TEST_CASE("ToolDefinitions: every definition has a handler with non-null fns") {
  for (const auto& def : ToolDefinitions()) {
    INFO("Tool: " << def.name);
    const auto* handler = LookupToolHandler(def.name);
    REQUIRE(handler != nullptr);
    CHECK(handler->prepare != nullptr);
    CHECK(handler->execute != nullptr);
  }
}

TEST_CASE("LookupToolHandler: unknown names return nullptr") {
  CHECK(LookupToolHandler("totally_unknown") == nullptr);
  CHECK(LookupToolHandler("") == nullptr);
  CHECK(LookupToolHandler("FILE_READ") == nullptr);
  CHECK(LookupToolHandler("file-read") == nullptr);
}

TEST_CASE("ToolDefinitions: parameter schemas parse as valid JSON") {
  for (const auto& def : ToolDefinitions()) {
    INFO("Tool: " << def.name);
    REQUIRE_NOTHROW(Json::parse(def.parameters_schema_json));
  }
}

TEST_CASE(
    "ToolDefinitions: parameter schemas are JSON objects with type:object") {
  for (const auto& def : ToolDefinitions()) {
    INFO("Tool: " << def.name);
    const auto schema = Json::parse(def.parameters_schema_json);
    REQUIRE(schema.is_object());
    CHECK(schema.value("type", std::string{}) == "object");
  }
}

TEST_CASE(
    "ToolDefinitions: parameter schemas declare at least one required field") {
  for (const auto& def : ToolDefinitions()) {
    INFO("Tool: " << def.name);
    const auto schema = Json::parse(def.parameters_schema_json);
    REQUIRE(schema.contains("required"));
    REQUIRE(schema["required"].is_array());
    CHECK_FALSE(schema["required"].empty());
  }
}

TEST_CASE("ToolDefinitions: schema round-trip serialization is stable") {
  for (const auto& def : ToolDefinitions()) {
    INFO("Tool: " << def.name);
    const std::string first = Json::parse(def.parameters_schema_json).dump();
    const std::string second = Json::parse(first).dump();
    CHECK(first == second);
  }
}

TEST_CASE("PrepareToolCall: approval flags match expected values") {
  SECTION("file_write requires approval") {
    auto prepared = PrepareToolCall(MakeRequest(
        "file_write", R"({"filepath":"out.txt","content":"hello"})"));
    CHECK(prepared.requires_approval == true);
  }

  SECTION("file_read does not require approval") {
    auto prepared = PrepareToolCall(
        MakeRequest("file_read", R"({"filepath":"README.md"})"));
    CHECK(prepared.requires_approval == false);
  }

  SECTION("bash requires approval") {
    auto prepared =
        PrepareToolCall(MakeRequest("bash", R"({"command":"echo hi"})"));
    CHECK(prepared.requires_approval == true);
  }

  SECTION("file_edit requires approval") {
    auto prepared = PrepareToolCall(MakeRequest(
        "file_edit",
        R"({"filepath":"x.cpp","old_string":"foo","new_string":"bar"})"));
    CHECK(prepared.requires_approval == true);
  }

  SECTION("lsp_rename requires approval") {
    auto prepared = PrepareToolCall(MakeRequest(
        "lsp_rename",
        R"({"file_path":"x.cpp","line":1,"character":5,"new_name":"y"})"));
    CHECK(prepared.requires_approval == true);
  }

  SECTION("grep does not require approval") {
    auto prepared =
        PrepareToolCall(MakeRequest("grep", R"({"pattern":"foo"})"));
    CHECK(prepared.requires_approval == false);
  }

  SECTION("glob does not require approval") {
    auto prepared =
        PrepareToolCall(MakeRequest("glob", R"({"pattern":"**/*.cpp"})"));
    CHECK(prepared.requires_approval == false);
  }

  SECTION("sub_agent does not require approval") {
    auto prepared =
        PrepareToolCall(MakeRequest("sub_agent", R"({"task":"do something"})"));
    CHECK(prepared.requires_approval == false);
  }

  SECTION("todo_write does not require approval") {
    auto prepared =
        PrepareToolCall(MakeRequest("todo_write", R"({"todos":[]})"));
    CHECK(prepared.requires_approval == false);
  }
}

TEST_CASE("PrepareToolCall: unknown tool name throws ToolValidationError") {
  CHECK_THROWS_AS(PrepareToolCall(MakeRequest("not_a_real_tool", "{}")),
                  ToolValidationError);
}

TEST_CASE("PrepareToolCall: missing required args throw ToolValidationError") {
  SECTION("file_read without filepath") {
    CHECK_THROWS_AS(PrepareToolCall(MakeRequest("file_read", "{}")),
                    ToolValidationError);
  }

  SECTION("bash without command") {
    CHECK_THROWS_AS(PrepareToolCall(MakeRequest("bash", "{}")),
                    ToolValidationError);
  }

  SECTION("file_write without content") {
    CHECK_THROWS_AS(
        PrepareToolCall(MakeRequest("file_write", R"({"filepath":"x.txt"})")),
        ToolValidationError);
  }

  SECTION("lsp_rename without new_name") {
    CHECK_THROWS_AS(
        PrepareToolCall(MakeRequest(
            "lsp_rename", R"({"file_path":"x.cpp","line":1,"character":0})")),
        ToolValidationError);
  }
}
