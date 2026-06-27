#include "core_types/chat_ids.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/executor_arguments.hpp"
#include "tool_call/executor_catalog.hpp"
#include "tool_call/todo_state.hpp"
#include "tool_call/tool_validation_error.hpp"
#include "tool_call/web_fetch.hpp"
#include "tool_call/web_search.hpp"
#include "tool_call/workspace_filesystem.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <filesystem>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

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
using yac::tool_call::WebFetchNetworkPolicy;
using yac::tool_call::WebFetchTransport;
using yac::tool_call::WebFetchTransportRequest;
using yac::tool_call::WebFetchTransportResponse;
using yac::tool_call::WebSearchProviderConfig;
using yac::tool_call::WebSearchTransport;
using yac::tool_call::WebSearchTransportRequest;
using yac::tool_call::WebSearchTransportResponse;

namespace {

ToolCallRequest MakeRequest(std::string name, std::string args_json) {
  return ToolCallRequest{.id = "t",
                         .name = std::move(name),
                         .arguments_json = std::move(args_json)};
}

class FakeWebFetchTransport final : public WebFetchTransport {
 public:
  WebFetchTransportResponse Fetch(const WebFetchTransportRequest& request,
                                  std::stop_token stop_token) override {
    (void)stop_token;
    called = true;
    observed_url = request.url;
    observed_timeout = request.timeout;
    for (const auto& header : headers) {
      request.on_header_line(header);
    }
    for (const auto& chunk : chunks) {
      request.on_body_chunk(chunk);
    }
    return {.status_code = status_code};
  }

  bool called = false;
  std::string observed_url;
  std::chrono::milliseconds observed_timeout{0};
  long status_code = 200;
  std::vector<std::string> headers{"Content-Type: text/plain\r\n"};
  std::vector<std::string> chunks{"example body"};
};

class FakeWebSearchTransport final : public WebSearchTransport {
 public:
  WebSearchTransportResponse Search(const WebSearchTransportRequest& request,
                                    std::stop_token stop_token) override {
    (void)stop_token;
    called = true;
    observed_endpoint = request.endpoint;
    observed_api_key = request.api_key;
    observed_body = request.body;
    observed_timeout = request.timeout;
    return response;
  }

  bool called = false;
  std::string observed_endpoint;
  std::string observed_api_key;
  std::string observed_body;
  std::chrono::milliseconds observed_timeout{0};
  WebSearchTransportResponse response{.status_code = 200,
                                      .content_type = "application/json"};
};

yac::chat::ToolDefinition RequireDefinition(std::string_view name) {
  const auto defs = ToolDefinitions();
  const auto it = std::ranges::find_if(
      defs, [name](const auto& def) { return def.name == name; });
  REQUIRE(it != defs.end());
  return *it;
}

ToolExecutionResult ExecuteThroughCatalog(
    const ToolHandler& handler, const ToolCallRequest& request,
    WebFetchTransport* web_fetch = nullptr,
    WebSearchTransport* web_search = nullptr,
    const std::optional<WebSearchProviderConfig>* web_search_config = nullptr) {
  auto prepared = handler.prepare(request, Json::parse(request.arguments_json));
  yac::tool_call::WorkspaceFilesystem workspace(
      std::filesystem::current_path());
  std::shared_ptr<yac::tool_call::ILspClient> lsp_client;
  yac::tool_call::TodoState todo_state;
  yac::tool_call::ExecutionContext context{
      .workspace_filesystem = workspace,
      .lsp_client = lsp_client,
      .todo_state = todo_state,
      .sub_agent_manager = nullptr,
      .tool_approval = nullptr,
      .web_fetch_transport = web_fetch,
      .web_fetch_network_policy =
          web_fetch == nullptr ? WebFetchNetworkPolicy::RealNetwork
                               : WebFetchNetworkPolicy::InjectedTransport,
      .web_search_transport = web_search,
      .web_search_config = web_search_config,
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

TEST_CASE("ToolDefinitions: ordered built-in contract snapshot is stable") {
  struct ExpectedDefinition {
    std::string_view name;
    std::string_view description_fragment;
    std::string_view schema_json;
  };

  constexpr std::array expected =
      {
          ExpectedDefinition{
              "file_read", "Read the contents",
              R"schema({"type":"object","additionalProperties":false,"properties":{"filepath":{"type":"string"}},"required":["filepath"]})schema"},
          ExpectedDefinition{
              "file_write", "fully overwrite",
              R"schema({"type":"object","additionalProperties":false,"properties":{"filepath":{"type":"string"},"content":{"type":"string"}},"required":["filepath","content"]})schema"},
          ExpectedDefinition{
              "list_dir", "List direct entries",
              R"schema({"type":"object","additionalProperties":false,"properties":{"path":{"type":"string"}},"required":["path"]})schema"},
          ExpectedDefinition{
              "lsp_diagnostics", "diagnostics",
              R"schema({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"}},"required":["file_path"]})schema"},
          ExpectedDefinition{
              "lsp_references", "Find references",
              R"schema({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"},"line":{"type":"integer"},"character":{"type":"integer"},"symbol":{"type":"string"}},"required":["file_path","line","character"]})schema"},
          ExpectedDefinition{
              "lsp_goto_definition", "Find definitions",
              R"schema({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"},"line":{"type":"integer"},"character":{"type":"integer"},"symbol":{"type":"string"}},"required":["file_path","line","character"]})schema"},
          ExpectedDefinition{
              "lsp_rename", "Rename a symbol",
              R"schema({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"},"line":{"type":"integer"},"character":{"type":"integer"},"old_name":{"type":"string"},"new_name":{"type":"string"}},"required":["file_path","line","character","new_name"]})schema"},
          ExpectedDefinition{
              "lsp_symbols", "document symbols",
              R"schema({"type":"object","additionalProperties":false,"properties":{"file_path":{"type":"string"}},"required":["file_path"]})schema"},
          ExpectedDefinition{
              "sub_agent", "Spawn a sub-agent",
              R"schema({"type":"object","additionalProperties":false,"properties":{"task":{"type":"string","description":"Detailed description of what the sub-agent should accomplish"},"mode":{"type":"string","enum":["foreground","background"],"description":"foreground blocks until complete and returns result. background runs in parallel and notifies when done."}},"required":["task"]})schema"},
          ExpectedDefinition{
              "todo_write", "todo list",
              R"schema({"type":"object","properties":{"todos":{"type":"array","items":{"type":"object","properties":{"content":{"type":"string","description":"Task description"},"status":{"type":"string","enum":["pending","in_progress","completed"],"description":"Current status"},"priority":{"type":"string","enum":["high","medium","low"],"description":"Priority level"}},"required":["content","status"]}}},"required":["todos"]})schema"},
          ExpectedDefinition{
              "ask_user", "Ask the user",
              R"schema({"type":"object","properties":{"question":{"type":"string","description":"The question to ask the user"},"options":{"type":"array","items":{"type":"string"},"description":"Optional suggested answers"}},"required":["question"]})schema"},
          ExpectedDefinition{
              "plan_exit", "switch to Build mode",
              R"schema({"type":"object","additionalProperties":false,"properties":{"plan":{"type":"string","description":"Final plan in markdown."}},"required":["plan"]})schema"},
          ExpectedDefinition{
              "bash", "Execute a shell command",
              R"schema({"type":"object","additionalProperties":false,"properties":{"command":{"type":"string","description":"Shell command to execute (passed to /bin/sh -c)"},"timeout_ms":{"type":"integer","description":"Timeout in milliseconds (default 30000, max 300000)","minimum":100,"maximum":300000}},"required":["command"]})schema"},
          ExpectedDefinition{
              "file_edit", "Edit a file",
              R"schema({"type":"object","additionalProperties":false,"properties":{"filepath":{"type":"string","description":"Workspace-relative or absolute path to the file"},"old_string":{"type":"string","description":"Exact text to replace (must not be empty)"},"new_string":{"type":"string","description":"Replacement text (can be empty to delete)"},"replace_all":{"type":"boolean","description":"Replace all occurrences (default false)"}},"required":["filepath","old_string","new_string"]})schema"},
          ExpectedDefinition{
              "grep",
              "ripgrep", R"schema({"type":"object","additionalProperties":false,"properties":{"pattern":{"type":"string","description":"Regex pattern (Rust regex syntax)"},"path":{"type":"string","description":"Path to search; defaults to workspace root"},"include":{"type":"string","description":"Glob filter for filenames (e.g. '*.cpp')"},"case_sensitive":{"type":"boolean","description":"Case-sensitive search (default false)"},"include_ignored":{"type":"boolean","description":"Include .gitignored files (default false)"}},"required":["pattern"]})schema"},
          ExpectedDefinition{
              "glob", "glob pattern",
              R"schema({"type":"object","additionalProperties":false,"properties":{"pattern":{"type":"string","description":"Glob pattern (e.g. 'src/**/*.hpp' or '*.cpp' for filenames at any depth)"},"path":{"type":"string","description":"Path to search; defaults to workspace root"},"include_ignored":{"type":"boolean","description":"Include .gitignored files (default false)"}},"required":["pattern"]})schema"},
          ExpectedDefinition{
              "web_fetch", "Fetch an HTTP(S) URL",
              R"schema({"type":"object","additionalProperties":false,"properties":{"url":{"type":"string","description":"HTTP(S) URL to fetch."},"format":{"type":"string","enum":["markdown","text","html"],"description":"Output format (default markdown)."},"timeout":{"type":"integer","description":"Timeout in seconds (default 30, max 120).","minimum":1,"maximum":120,"default":30}},"required":["url"]})schema"},
          ExpectedDefinition{
              "web_search", "Exa provider",
              R"schema({"type":"object","additionalProperties":false,"properties":{"query":{"type":"string","description":"Search query to send to the configured provider."},"num_results":{"type":"integer","description":"Number of results to request (default 5, max 10).","minimum":1,"maximum":10,"default":5},"context_max_characters":{"type":"integer","description":"Maximum provider context characters per request (default 4096, max 12000).","minimum":1,"maximum":12000,"default":4096}},"required":["query"]})schema"},
      };

  const auto defs = ToolDefinitions();
  REQUIRE(defs.size() == expected.size());
  for (size_t i = 0; i < expected.size(); ++i) {
    INFO(expected[i].name);
    CHECK(defs[i].name == expected[i].name);
    CHECK(defs[i].description.find(expected[i].description_fragment) !=
          std::string::npos);
    CHECK(Json::parse(defs[i].parameters_schema_json) ==
          Json::parse(expected[i].schema_json));
    REQUIRE(LookupToolHandler(expected[i].name) != nullptr);
  }
}

TEST_CASE("ToolDefinitions: every handler appears in ordered snapshot") {
  for (const auto& def : ToolDefinitions()) {
    INFO(def.name);
    REQUIRE(LookupToolHandler(def.name) != nullptr);
  }
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

TEST_CASE("ToolDefinitions: web tools declare expected schemas") {
  SECTION("web_fetch") {
    const auto& def = RequireDefinition("web_fetch");
    CHECK(def.description.find("Fetch") != std::string::npos);
    CHECK(def.description.find("30 second timeout") != std::string::npos);

    const auto schema = Json::parse(def.parameters_schema_json);
    REQUIRE(schema["required"].size() == 1);
    CHECK(schema["required"][0] == "url");
    CHECK(schema["properties"]["url"]["type"] == "string");
    CHECK(schema["properties"]["format"]["type"] == "string");
    CHECK(schema["properties"]["format"]["enum"] ==
          Json::array({"markdown", "text", "html"}));
    CHECK(schema["properties"]["timeout"]["type"] == "integer");
    CHECK(schema["properties"]["timeout"]["default"] == 30);
    CHECK(schema["properties"]["timeout"]["maximum"] == 120);
  }

  SECTION("web_search") {
    const auto& def = RequireDefinition("web_search");
    CHECK(def.description.find("Search") != std::string::npos);
    CHECK(def.description.find("Exa") != std::string::npos);

    const auto schema = Json::parse(def.parameters_schema_json);
    REQUIRE(schema["required"].size() == 1);
    CHECK(schema["required"][0] == "query");
    CHECK(schema["properties"]["query"]["type"] == "string");
    CHECK(schema["properties"]["num_results"]["type"] == "integer");
    CHECK(schema["properties"]["num_results"]["default"] == 5);
    CHECK(schema["properties"]["num_results"]["maximum"] == 10);
    CHECK(schema["properties"]["context_max_characters"]["type"] == "integer");
    CHECK(schema["properties"]["context_max_characters"]["default"] == 4096);
    CHECK(schema["properties"]["context_max_characters"]["maximum"] == 12000);
  }
}

TEST_CASE("LookupToolHandler: web tools execute through registered handlers") {
  SECTION("web_fetch") {
    const auto* handler = LookupToolHandler("web_fetch");
    REQUIRE(handler != nullptr);
    FakeWebFetchTransport transport;
    transport.headers = {"Content-Type: text/html\r\n"};
    transport.chunks = {"<h1>Hello</h1><p>catalog</p>"};

    const auto result = ExecuteThroughCatalog(
        *handler,
        MakeRequest(
            "web_fetch",
            R"({"url":"https://example.test/page","format":"text","timeout":45})"),
        &transport);

    REQUIRE_FALSE(result.is_error);
    const auto payload = Json::parse(result.result_json);
    CHECK(payload["url"] == "https://example.test/page");
    CHECK(payload["body"].get<std::string>().find("catalog") !=
          std::string::npos);
    CHECK(transport.called);
    CHECK(transport.observed_url == "https://example.test/page");
    CHECK(transport.observed_timeout == std::chrono::seconds(45));
    REQUIRE(std::holds_alternative<yac::tool_call::WebFetchCall>(result.block));
    const auto& call = std::get<yac::tool_call::WebFetchCall>(result.block);
    CHECK(call.url == "https://example.test/page");
    CHECK(call.format == "text");
    CHECK(call.timeout == 45);
  }

  SECTION("web_search") {
    const auto* handler = LookupToolHandler("web_search");
    REQUIRE(handler != nullptr);
    FakeWebSearchTransport transport;
    transport.response.body =
        Json{{"results", Json::array({{{"title", "First"},
                                       {"url", "https://example.test/first"},
                                       {"snippet", "first snippet"}},
                                      {{"title", "Second"},
                                       {"url", "https://example.test/second"},
                                       {"snippet", "second snippet"}}})}}
            .dump();
    const std::optional<WebSearchProviderConfig> config{
        WebSearchProviderConfig{.endpoint = "https://exa.test/search",
                                .api_key = "fake-exa-key",
                                .timeout_seconds = 25,
                                .result_limit = 8,
                                .context_limit = 9000}};

    const auto result = ExecuteThroughCatalog(
        *handler,
        MakeRequest(
            "web_search",
            R"({"query":"yac terminal","num_results":2,"context_max_characters":2048})"),
        nullptr, &transport, &config);

    REQUIRE_FALSE(result.is_error);
    const auto payload = Json::parse(result.result_json);
    CHECK(payload["provider"] == "exa");
    CHECK(payload["metadata"]["provider"] == "exa");
    const std::string output = payload["output"];
    CHECK(output.find("First") < output.find("Second"));
    CHECK(output.find("https://example.test/first") != std::string::npos);
    CHECK(output.find("https://example.test/second") != std::string::npos);
    CHECK(transport.called);
    CHECK(transport.observed_endpoint == "https://exa.test/search");
    CHECK(transport.observed_timeout == std::chrono::seconds(25));
    const auto observed_body = Json::parse(transport.observed_body);
    CHECK(observed_body["num_results"] == 2);
    CHECK(observed_body["context_max_characters"] == 2048);
    REQUIRE(
        std::holds_alternative<yac::tool_call::WebSearchCall>(result.block));
    const auto& call = std::get<yac::tool_call::WebSearchCall>(result.block);
    CHECK(call.query == "yac terminal");
    CHECK(call.num_results == 2);
    CHECK(call.context_max_characters == 2048);
    REQUIRE(call.results.size() == 2);
    CHECK(call.results[0].title == "First");
  }

  SECTION("web_search uses configured defaults when limits are omitted") {
    const auto* handler = LookupToolHandler("web_search");
    REQUIRE(handler != nullptr);
    FakeWebSearchTransport transport;
    transport.response.body = Json{{"results", Json::array()}}.dump();
    const std::optional<WebSearchProviderConfig> config{
        WebSearchProviderConfig{.endpoint = "https://exa.test/search",
                                .api_key = "fake-exa-key",
                                .timeout_seconds = 25,
                                .result_limit = 7,
                                .context_limit = 9000}};

    const auto result = ExecuteThroughCatalog(
        *handler, MakeRequest("web_search", R"({"query":"yac terminal"})"),
        nullptr, &transport, &config);

    REQUIRE_FALSE(result.is_error);
    REQUIRE(transport.called);
    const auto observed_body = Json::parse(transport.observed_body);
    CHECK(observed_body["num_results"] == 7);
    CHECK(observed_body["context_max_characters"] == 9000);
    REQUIRE(
        std::holds_alternative<yac::tool_call::WebSearchCall>(result.block));
    const auto& call = std::get<yac::tool_call::WebSearchCall>(result.block);
    CHECK(call.num_results == 7);
    CHECK(call.context_max_characters == 9000);
  }

  SECTION("web_search without config") {
    const auto* handler = LookupToolHandler("web_search");
    REQUIRE(handler != nullptr);

    const auto result = ExecuteThroughCatalog(
        *handler, MakeRequest("web_search", R"({"query":"yac terminal"})"));

    REQUIRE(result.is_error);
    CHECK(Json::parse(result.result_json)["error"] ==
          "web_search provider is not configured");
  }
}

TEST_CASE("LookupToolHandler: web_fetch validation errors keep tool context") {
  CHECK_THROWS_AS(PrepareToolCall(MakeRequest("web_fetch", R"({"url":5})")),
                  ToolValidationError);

  try {
    (void)PrepareToolCall(MakeRequest(
        "web_fetch", R"({"url":"https://example.test/page","format":"pdf"})"));
    FAIL("Expected web_fetch format validation to throw");
  } catch (const ToolValidationError& error) {
    CHECK(std::string(error.what()).find("web_fetch format") !=
          std::string::npos);
    CHECK(error.tool_name() == "web_fetch");
    CHECK(error.raw_arguments_json().find("example.test") != std::string::npos);
  }
}
