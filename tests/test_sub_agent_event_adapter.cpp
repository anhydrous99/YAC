#include "chat/sub_agent_event_adapter.hpp"
#include "core_types/typed_ids.hpp"

#include <atomic>
#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using namespace yac::chat;
using namespace yac::tool_call;
using yac::SubAgentId;

TEST_CASE(
    "AdaptSubAgentPromptEvent rewrites approval prompts for the parent card") {
  const SubAgentEventContext context{
      .card_message_id = 99,
      .agent_id = "agent-1",
      .task = "review repository structure before requesting approval",
  };
  std::atomic<int> completed_tool_count{0};

  auto adapted = AdaptSubAgentPromptEvent(
      context,
      ChatEvent{ToolApprovalRequestedEvent{
          .message_id = 7,
          .text = "Allow reading the workspace?",
          .tool_call_id = yac::ToolCallId{"approval-1"},
          .tool_name = "file_read",
          .approval_id = yac::ApprovalId{"request-1"},
          .tool_call = FileReadCall{.filepath = "README.md",
                                    .lines_loaded = 12,
                                    .excerpt = "intro"}}},
      completed_tool_count);

  REQUIRE(adapted.has_value());
  REQUIRE(adapted->Type() == ChatEventType::ToolApprovalRequested);
  const auto& approval = adapted->Get<ToolApprovalRequestedEvent>();
  REQUIRE(approval.message_id == 99);
  REQUIRE_THAT(approval.text,
               Catch::Matchers::StartsWith("[Sub-agent: review repository"));
  REQUIRE_THAT(approval.text, Catch::Matchers::ContainsSubstring(
                                  "Allow reading the workspace?"));
}

TEST_CASE(
    "AdaptSubAgentPromptEvent maps child tool lifecycle into progress events") {
  const SubAgentEventContext context{
      .card_message_id = 50,
      .agent_id = "agent-2",
      .task = "inspect source tree",
  };
  std::atomic<int> completed_tool_count{0};

  SECTION("tool started") {
    auto adapted = AdaptSubAgentPromptEvent(
        context,
        ChatEvent{
            ToolCallStartedEvent{.message_id = 11,
                                 .tool_name = "list_dir",
                                 .tool_call = ListDirCall{.path = "src",
                                                          .entries = {},
                                                          .truncated = false,
                                                          .is_error = false,
                                                          .error = ""},
                                 .status = ChatMessageStatus::Active}},
        completed_tool_count);

    REQUIRE(adapted.has_value());
    REQUIRE(adapted->Type() == ChatEventType::SubAgentProgress);
    const auto& progress = adapted->Get<SubAgentProgressEvent>();
    REQUIRE(progress.message_id == 50);
    REQUIRE(progress.sub_agent_id == SubAgentId{"agent-2"});
    REQUIRE(progress.sub_agent_task == "inspect source tree");
    REQUIRE(progress.sub_agent_tool_count == 0);
    REQUIRE(progress.child_tool.has_value());
    REQUIRE(progress.child_tool->tool_call_id == yac::ToolCallId{"agent-2:11"});
    REQUIRE(progress.child_tool->tool_name == "list_dir");
    REQUIRE(progress.child_tool->status == ChatMessageStatus::Active);
  }

  SECTION("tool completed") {
    auto adapted = AdaptSubAgentPromptEvent(
        context,
        ChatEvent{
            ToolCallDoneEvent{.message_id = 12,
                              .tool_call_id = yac::ToolCallId{"call-12"},
                              .tool_name = "file_read",
                              .tool_call = FileReadCall{.filepath = "README.md",
                                                        .lines_loaded = 20,
                                                        .excerpt = "docs"},
                              .status = ChatMessageStatus::Complete}},
        completed_tool_count);

    REQUIRE(adapted.has_value());
    REQUIRE(adapted->Type() == ChatEventType::SubAgentProgress);
    const auto& progress = adapted->Get<SubAgentProgressEvent>();
    REQUIRE(progress.sub_agent_tool_count == 1);
    REQUIRE(progress.child_tool.has_value());
    REQUIRE(progress.child_tool->tool_call_id == yac::ToolCallId{"call-12"});
    REQUIRE(progress.child_tool->tool_name == "file_read");
    REQUIRE(progress.child_tool->status == ChatMessageStatus::Complete);
    REQUIRE(completed_tool_count.load() == 1);
  }
}

TEST_CASE(
    "MakeSubAgentCompletionEvent emits the requested terminal event type") {
  SECTION("completed") {
    const auto event = MakeSubAgentCompletionEvent(SubAgentCompletionEventData{
        .type = ChatEventType::SubAgentCompleted,
        .message_id = 61,
        .sub_agent_id = SubAgentId{"agent-3"},
        .sub_agent_task = "run tests",
        .sub_agent_result = "all passed",
        .sub_agent_tool_count = 4,
        .sub_agent_elapsed_ms = 900,
    });

    REQUIRE(event.Type() == ChatEventType::SubAgentCompleted);
    const auto& payload = event.Get<SubAgentCompletedEvent>();
    REQUIRE(payload.message_id == 61);
    REQUIRE(payload.sub_agent_result == "all passed");
    REQUIRE(payload.sub_agent_tool_count == 4);
    REQUIRE(payload.sub_agent_elapsed_ms == 900);
  }

  SECTION("error") {
    const auto event = MakeSubAgentCompletionEvent(SubAgentCompletionEventData{
        .type = ChatEventType::SubAgentError,
        .message_id = 62,
        .sub_agent_id = SubAgentId{"agent-4"},
        .sub_agent_task = "fetch remote state",
        .sub_agent_result = "connection refused",
        .sub_agent_tool_count = 2,
        .sub_agent_elapsed_ms = 300,
    });

    REQUIRE(event.Type() == ChatEventType::SubAgentError);
    const auto& payload = event.Get<SubAgentErrorEvent>();
    REQUIRE(payload.message_id == 62);
    REQUIRE(payload.sub_agent_result == "connection refused");
    REQUIRE(payload.sub_agent_tool_count == 2);
    REQUIRE(payload.sub_agent_elapsed_ms == 300);
  }

  SECTION("cancelled") {
    const auto event = MakeSubAgentCompletionEvent(SubAgentCompletionEventData{
        .type = ChatEventType::SubAgentCancelled,
        .message_id = 63,
        .sub_agent_id = SubAgentId{"agent-5"},
        .sub_agent_task = "long running task",
    });

    REQUIRE(event.Type() == ChatEventType::SubAgentCancelled);
    const auto& payload = event.Get<SubAgentCancelledEvent>();
    REQUIRE(payload.message_id == 63);
    REQUIRE(payload.sub_agent_id == SubAgentId{"agent-5"});
    REQUIRE(payload.sub_agent_task == "long running task");
  }
}
