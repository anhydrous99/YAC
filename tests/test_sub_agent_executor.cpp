#include "chat/sub_agent_manager.hpp"
#include "tool_call/executor.hpp"
#include "tool_call/sub_agent_tool_executor.hpp"

#include <algorithm>
#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;
using namespace yac::tool_call;

TEST_CASE("sub_agent tool appears in definitions") {
  auto defs = ToolExecutor::Definitions();
  auto it = std::ranges::find_if(
      defs, [](const auto& d) { return d.name == "sub_agent"; });
  REQUIRE(it != defs.end());
  REQUIRE(it->description.find("sub-agent") != std::string::npos);
}

TEST_CASE("Prepare sub_agent tool creates SubAgentCall preview") {
  ToolCallRequest request{
      .id = "call-1",
      .name = "sub_agent",
      .arguments_json = R"({"task":"analyze the codebase"})"};
  auto prepared = ToolExecutor::Prepare(request);
  REQUIRE(std::holds_alternative<SubAgentCall>(prepared.preview));
  const auto& call = std::get<SubAgentCall>(prepared.preview);
  REQUIRE(call.task == "analyze the codebase");
  REQUIRE(call.status == SubAgentStatus::Pending);
  REQUIRE(prepared.requires_approval == false);
}

TEST_CASE("Prepare sub_agent with missing task returns error") {
  ToolCallRequest request{
      .id = "call-2", .name = "sub_agent", .arguments_json = R"({})"};
  auto prepared = ToolExecutor::Prepare(request);
  REQUIRE(std::holds_alternative<BashCall>(prepared.preview));
  const auto& bash = std::get<BashCall>(prepared.preview);
  REQUIRE(bash.is_error);
}

TEST_CASE("sub_agent tool requires no approval") {
  ToolCallRequest request{.id = "call-3",
                          .name = "sub_agent",
                          .arguments_json = R"({"task":"check types"})"};
  auto prepared = ToolExecutor::Prepare(request);
  REQUIRE(prepared.requires_approval == false);
}

TEST_CASE("ExecuteSubAgentTool returns error when manager is null") {
  ToolCallRequest request{.id = "call-4",
                          .name = "sub_agent",
                          .arguments_json = R"({"task":"some task"})"};
  auto prepared = ToolExecutor::Prepare(request);
  auto result = ExecuteSubAgentTool(prepared, nullptr);
  REQUIRE(result.is_error);
}

TEST_CASE("kMaxResultChars is defined as 4096") {
  static_assert(kMaxResultChars == 4096,
                "kMaxResultChars must be 4096 per spec");
  REQUIRE(kMaxResultChars == 4096);
}
