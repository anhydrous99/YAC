#include "chat/chat_service_tool_approval.hpp"
#include "chat/types.hpp"
#include "tool_call/executor_catalog.hpp"

#include <chrono>
#include <stop_token>
#include <thread>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace std::chrono_literals;

using namespace yac::chat;
using namespace yac::chat::internal;
using namespace yac::tool_call;

TEST_CASE("Ask user preparation and approval resolution work") {
  SECTION("PrepareToolCall sets requires_approval for ask_user") {
    ToolCallRequest request{
        .id = "call-1",
        .name = std::string(kAskUserToolName),
        .arguments_json = R"({"question":"Ship it?","options":["yes","no"]})"};

    auto prepared = PrepareToolCall(request);

    REQUIRE(prepared.requires_approval);
    REQUIRE(prepared.approval_prompt == "Ship it?");
  }

  SECTION("AskUserCall struct populated with question and options") {
    ToolCallRequest request{
        .id = "call-2",
        .name = std::string(kAskUserToolName),
        .arguments_json =
            R"({"question":"Pick one","options":["alpha","beta"]})"};

    auto prepared = PrepareToolCall(request);

    REQUIRE(std::holds_alternative<AskUserCall>(prepared.preview));
    const auto& call = std::get<AskUserCall>(prepared.preview);
    REQUIRE(call.question == "Pick one");
    const std::vector<std::string> expected_options{"alpha", "beta"};
    REQUIRE(call.options == expected_options);
  }

  SECTION("ResolveWithResponse wakes WaitForResolution with response") {
    ChatServiceToolApproval approval;
    auto approval_id = approval.BeginPendingApproval();

    ApprovalResolution resolution;
    std::jthread waiter([&](std::stop_token stop_token) {
      resolution = approval.WaitForResolution(approval_id, stop_token);
    });

    std::this_thread::sleep_for(10ms);
    approval.ResolveWithResponse(approval_id, true, "Need one more change");
    waiter.join();

    REQUIRE(resolution.approved);
    REQUIRE(resolution.response == "Need one more change");
  }

  SECTION("ResolveToolApproval with false cancels") {
    ChatServiceToolApproval approval;
    auto approval_id = approval.BeginPendingApproval();

    ApprovalResolution resolution;
    std::jthread waiter([&](std::stop_token stop_token) {
      resolution = approval.WaitForResolution(approval_id, stop_token);
    });

    std::this_thread::sleep_for(10ms);
    approval.Resolve(approval_id, false);
    waiter.join();

    REQUIRE_FALSE(resolution.approved);
    REQUIRE(resolution.response.empty());
  }
}
