#include "core_types/chat_ids.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/executor_arguments.hpp"
#include "tool_call/executor_catalog.hpp"
#include "tool_call/todo_state.hpp"
#include "tool_call/tool_validation_error.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <algorithm>
#include <filesystem>
#include <memory>
#include <stop_token>
#include <string>
#include <unordered_set>
#include <variant>

#include <catch2/catch_test_macros.hpp>

using yac::chat::ToolCallRequest;
using yac::tool_call::Json;
using yac::tool_call::LookupToolHandler;
using yac::tool_call::PrepareToolCall;
using yac::tool_call::ToolDefinitions;
using yac::tool_call::ToolExecutionResult;
using yac::tool_call::ToolHandler;
using yac::tool_call::ToolHandlerCount;
using yac::tool_call::ToolValidationError;

namespace {

ToolCallRequest MakeRequest(std::string name, std::string args_json) {
  return ToolCallRequest{.id = "t",
                         .name = std::move(name),
                         .arguments_json = std::move(args_json)};
}

yac::chat::ToolDefinition RequireDefinition(std::string_view name) {
  const auto defs = ToolDefinitions();
  const auto it = std::ranges::find_if(
      defs, [name](const auto& def) { return def.name == name; });
  REQUIRE(it != defs.end());
  return *it;
}

ToolExecutionResult ExecuteThroughCatalog(const ToolHandler& handler,
                                          const ToolCallRequest& request) {
  auto prepared = handler.prepare(request, Json::parse(request.arguments_json));
  yac::tool_call::WorkspaceFilesystem workspace(
      std::filesystem::current_path());
  std::shared_ptr<yac::tool_call::ILspClient> lsp_client;
  yac::tool_call::TodoState todo_state;
  yac::tool_call::ExecutionContext context{.workspace_filesystem = workspace,
                                           .lsp_client = lsp_client,
                                           .todo_state = todo_state,
                                           .sub_agent_manager = nullptr,
                                           .tool_approval = nullptr,
                                           .stop = std::stop_token{}};
  return handler.execute(prepared, context);
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

  SECTION("plan_exit requires approval") {
    auto prepared =
        PrepareToolCall(MakeRequest("plan_exit", R"({"plan":"# Final plan"})"));
    CHECK(prepared.requires_approval == true);
    CHECK(prepared.approval_prompt == "Approve plan and switch to Build mode?");
    REQUIRE(
        std::holds_alternative<yac::tool_call::PlanExitCall>(prepared.preview));
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

TEST_CASE(
    "ToolDefinitions: plan_exit schema has exactly required plan string") {
  const auto defs = ToolDefinitions();
  const auto it = std::ranges::find_if(
      defs, [](const auto& def) { return def.name == "plan_exit"; });
  REQUIRE(it != defs.end());

  const auto schema = Json::parse(it->parameters_schema_json);
  REQUIRE(schema["required"].size() == 1);
  REQUIRE(schema["required"][0] == "plan");
  REQUIRE(schema["properties"].size() == 1);
  REQUIRE(schema["properties"].contains("plan"));
  REQUIRE(schema["properties"]["plan"]["type"] == "string");
}

TEST_CASE("ToolDefinitions: web tools are declared as unsupported built-ins") {
  SECTION("web_fetch") {
    const auto& def = RequireDefinition("web_fetch");
    CHECK(def.description.find("unsupported") != std::string::npos);
    CHECK(def.description.find("not configured") != std::string::npos);

    const auto schema = Json::parse(def.parameters_schema_json);
    REQUIRE(schema["required"].size() == 1);
    CHECK(schema["required"][0] == "url");
    CHECK(schema["properties"]["url"]["type"] == "string");
  }

  SECTION("web_search") {
    const auto& def = RequireDefinition("web_search");
    CHECK(def.description.find("unsupported") != std::string::npos);
    CHECK(def.description.find("not configured") != std::string::npos);

    const auto schema = Json::parse(def.parameters_schema_json);
    REQUIRE(schema["required"].size() == 1);
    CHECK(schema["required"][0] == "query");
    CHECK(schema["properties"]["query"]["type"] == "string");
  }
}

TEST_CASE("LookupToolHandler: web tools return stable unsupported errors") {
  constexpr std::string_view kUnsupportedWebToolError =
      "Built-in web tools are unsupported and not configured in YAC.";

  SECTION("web_fetch") {
    const auto* handler = LookupToolHandler("web_fetch");
    REQUIRE(handler != nullptr);

    const auto result = ExecuteThroughCatalog(
        *handler, MakeRequest("web_fetch", R"({"url":"https://example.com"})"));

    REQUIRE(result.is_error);
    CHECK(Json::parse(result.result_json)["error"] == kUnsupportedWebToolError);
    REQUIRE(std::holds_alternative<yac::tool_call::WebFetchCall>(result.block));
    CHECK(std::get<yac::tool_call::WebFetchCall>(result.block).url ==
          "https://example.com");
  }

  SECTION("web_search") {
    const auto* handler = LookupToolHandler("web_search");
    REQUIRE(handler != nullptr);

    const auto result = ExecuteThroughCatalog(
        *handler, MakeRequest("web_search", R"({"query":"yac terminal"})"));

    REQUIRE(result.is_error);
    CHECK(Json::parse(result.result_json)["error"] == kUnsupportedWebToolError);
    REQUIRE(
        std::holds_alternative<yac::tool_call::WebSearchCall>(result.block));
    CHECK(std::get<yac::tool_call::WebSearchCall>(result.block).query ==
          "yac terminal");
  }
}
