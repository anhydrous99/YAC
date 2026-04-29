#include "presentation/tool_call/descriptor.hpp"
#include "tool_call/types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation::tool_call;
using namespace yac::tool_call;

TEST_CASE("DescribeToolCall summarizes edited files by basename") {
  const auto descriptor = DescribeToolCall(
      FileEditCall{"src/presentation/tool_call/renderer.cpp",
                   {{DiffLine::Add, "added"}, {DiffLine::Remove, "removed"}}});

  REQUIRE(descriptor.tag == "edit");
  REQUIRE(descriptor.label == "Edit renderer.cpp");
  REQUIRE(descriptor.summary == "2 lines");
}

TEST_CASE("DescribeToolCall reports write failures and grep counts") {
  SECTION("failed write") {
    const auto descriptor =
        DescribeToolCall(FileWriteCall{.filepath = "tmp/output.txt",
                                       .is_error = true,
                                       .error = "permission denied"});

    REQUIRE(descriptor.tag == "write");
    REQUIRE(descriptor.label == "Write output.txt");
    REQUIRE(descriptor.summary == "failed");
  }

  SECTION("streaming write") {
    const auto descriptor =
        DescribeToolCall(FileWriteCall{.filepath = "tmp/output.txt",
                                       .content_preview = "partial",
                                       .is_streaming = true});

    REQUIRE(descriptor.tag == "write");
    REQUIRE(descriptor.summary == "streaming…");
  }

  SECTION("grep count") {
    const auto descriptor = DescribeToolCall(
        GrepCall{"needle", 3, {{"src/main.cpp", 12, "needle hit"}}});

    REQUIRE(descriptor.tag == "grep");
    REQUIRE(descriptor.label == "Search for \"needle\"");
    REQUIRE(descriptor.summary == "3 matches");
  }
}

TEST_CASE("DescribeToolCall summarizes sub-agent status") {
  const auto descriptor = DescribeToolCall(SubAgentCall{
      "run focused tests", SubAgentMode::Background, SubAgentStatus::Complete,
      "agent-7", "all green", "", 4, 1200});

  REQUIRE(descriptor.tag == "agent");
  REQUIRE(descriptor.label == "[>] Sub-agent");
  REQUIRE(descriptor.summary == "Sub-agent: run focused tests - done");
}

TEST_CASE("DescribeToolCall summarizes MCP tools") {
  SECTION("result count") {
    const auto descriptor = DescribeToolCall(McpToolCall{
        .server_id = "github",
        .tool_name = "search_repos",
        .result_blocks = {{.kind = McpResultBlockKind::Text},
                          {.kind = McpResultBlockKind::ResourceLink}},
    });

    REQUIRE(descriptor.tag == "mcp");
    REQUIRE(descriptor.label == "[MCP: github] search_repos");
    REQUIRE(descriptor.summary == "2 results");
  }

  SECTION("empty result") {
    const auto descriptor = DescribeToolCall(McpToolCall{
        .server_id = "fs",
        .tool_name = "read_empty",
    });

    REQUIRE(descriptor.summary == "no result");
  }

  SECTION("failure and truncation") {
    auto failed =
        McpToolCall{.server_id = "bad", .tool_name = "fail", .is_error = true};
    auto truncated = McpToolCall{
        .server_id = "large", .tool_name = "query", .is_truncated = true};

    REQUIRE(DescribeToolCall(failed).summary == "failed");
    REQUIRE(DescribeToolCall(truncated).summary == "truncated");
  }
}
