#include "chat/chat_service_mcp.hpp"
#include "core_types/mcp_tool_catalog_snapshot.hpp"
#include "core_types/tool_call_types.hpp"
#include "mock_mcp_manager.hpp"

#include <algorithm>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat::internal;
using namespace yac::chat;
using namespace yac::tool_call;
using yac::test::MockMcpManager;

TEST_CASE("merge_builtins_and_mcp") {
  MockMcpManager mock;
  mock.AddTool("search", "query");
  mock.AddTool("db", "read");
  ChatServiceMcp helper{&mock};

  const auto snapshot = helper.BuildToolCatalogSnapshot();

  SECTION("merged vector contains all built-ins and all mcp tools") {
    std::vector<ToolDefinition> built_ins{
        ToolDefinition{.name = "file_read",
                       .description = "Read file",
                       .parameters_schema_json = "{}"},
        ToolDefinition{.name = "bash",
                       .description = "Execute bash",
                       .parameters_schema_json = "{}"}};

    const auto merged =
        ChatServiceMcp::MergeBuiltInsAndMcp(built_ins, snapshot);

    REQUIRE(merged.size() == 4);

    auto has_name = [&](const std::string& name) {
      return std::ranges::any_of(merged,
                                 [&](const auto& d) { return d.name == name; });
    };
    CHECK(has_name("file_read"));
    CHECK(has_name("bash"));
    CHECK(has_name("mcp_search__query"));
    CHECK(has_name("mcp_db__read"));
  }

  SECTION("no duplicates when built-ins and mcp tools are disjoint") {
    std::vector<ToolDefinition> built_ins{ToolDefinition{
        .name = "glob", .description = "Glob", .parameters_schema_json = "{}"}};

    const auto merged =
        ChatServiceMcp::MergeBuiltInsAndMcp(built_ins, snapshot);

    std::vector<std::string> names;
    names.reserve(merged.size());
    for (const auto& d : merged) {
      names.push_back(d.name);
    }
    std::ranges::sort(names);
    REQUIRE(std::ranges::adjacent_find(names) == names.end());
  }

  SECTION("collision raises validation error") {
    std::vector<ToolDefinition> built_ins{
        ToolDefinition{.name = "mcp_search__query",
                       .description = "collision",
                       .parameters_schema_json = "{}"}};

    REQUIRE_THROWS_AS(ChatServiceMcp::MergeBuiltInsAndMcp(built_ins, snapshot),
                      std::invalid_argument);
  }
}

TEST_CASE("prepare_execute_round_trip") {
  MockMcpManager mock;
  mock.AddTool("fs", "list_files");
  mock.SetInvokeResult(R"({"files":["a.cpp","b.hpp"]})");
  ChatServiceMcp helper{&mock};

  ToolCallRequest request{.id = "call-1",
                          .name = "mcp_fs__list_files",
                          .arguments_json = R"({"path":"/src"})"};

  SECTION("Prepare returns PreparedToolCall with McpToolCall block") {
    const auto prepared = helper.PrepareMcpToolCall(request);

    CHECK(prepared.request.name == "mcp_fs__list_files");
    CHECK(prepared.request.id == "call-1");

    const auto* mcp_block = std::get_if<McpToolCall>(&prepared.preview);
    REQUIRE(mcp_block != nullptr);
    CHECK(mcp_block->server_id == "fs");
    CHECK(mcp_block->original_tool_name == "list_files");
    CHECK(mcp_block->tool_name == "mcp_fs__list_files");
    CHECK(mcp_block->arguments_json == R"({"path":"/src"})");
  }

  SECTION("Execute returns ToolExecutionResult with result_blocks populated") {
    const auto prepared = helper.PrepareMcpToolCall(request);

    std::stop_source stop_src;
    const auto result =
        helper.ExecuteMcpToolCall(prepared, stop_src.get_token());

    CHECK(!result.is_error);
    CHECK(result.result_json == R"({"files":["a.cpp","b.hpp"]})");

    const auto* mcp_block = std::get_if<McpToolCall>(&result.block);
    REQUIRE(mcp_block != nullptr);
    REQUIRE(!mcp_block->result_blocks.empty());
    CHECK(mcp_block->result_blocks[0].kind == McpResultBlockKind::Text);
    CHECK(mcp_block->result_blocks[0].text == R"({"files":["a.cpp","b.hpp"]})");
  }

  SECTION("MockMcpManager invoke_count == 1 after single Execute") {
    const auto prepared = helper.PrepareMcpToolCall(request);

    std::stop_source stop_src;
    static_cast<void>(
        helper.ExecuteMcpToolCall(prepared, stop_src.get_token()));

    CHECK(mock.invoke_count == 1);
  }
}
