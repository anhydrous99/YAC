#include "app/chat_event_bridge.hpp"
#include "presentation/chat_ui.hpp"
#include "tool_call/types.hpp"

#include <variant>

#include <catch2/catch_test_macros.hpp>

using namespace yac::app;
using namespace yac::chat;
using namespace yac::presentation;
using namespace yac::tool_call;

namespace {

ChatEvent MakeToolStartedEvent(ChatMessageId id, std::string agent_id,
                               std::string task) {
  return ChatEvent{
      .type = ChatEventType::ToolCallStarted,
      .message_id = id,
      .role = ChatRole::Tool,
      .tool_name = "sub_agent",
      .tool_call = SubAgentCall{.task = task,
                                .status = SubAgentStatus::Running,
                                .agent_id = std::move(agent_id)},
      .status = ChatMessageStatus::Active,
      .sub_agent_task = std::move(task),
  };
}

}  // namespace

TEST_CASE(
    "Bridge creates a single card via ToolCallStarted with SubAgentCall") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(50, "agent-1", "analyze"));

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].id == 50);
  REQUIRE(ui.GetMessages()[0].sender == Sender::Tool);
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Active);
  const auto* call = ui.GetMessages()[0].ToolCall();
  REQUIRE(call != nullptr);
  const auto& sub = std::get<SubAgentCall>(*call);
  REQUIRE(sub.task == "analyze");
  REQUIRE(sub.status == SubAgentStatus::Running);
  REQUIRE(sub.agent_id == "agent-1");
}

TEST_CASE(
    "Bridge updates the same card on SubAgentCompleted and shows "
    "notification") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(51, "agent-2", "run all tests"));

  bridge.HandleEvent(ChatEvent{
      .type = ChatEventType::SubAgentCompleted,
      .message_id = 51,
      .sub_agent_id = "agent-2",
      .sub_agent_task = "run all tests",
      .sub_agent_result = "all 42 tests passed",
      .sub_agent_tool_count = 5,
      .sub_agent_elapsed_ms = 1200,
  });

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Complete);
  const auto* call = ui.GetMessages()[0].ToolCall();
  REQUIRE(call != nullptr);
  const auto& sub = std::get<SubAgentCall>(*call);
  REQUIRE(sub.status == SubAgentStatus::Complete);
  REQUIRE(sub.result == "all 42 tests passed");
  REQUIRE(sub.tool_count == 5);
  REQUIRE(sub.elapsed_ms == 1200);
}

TEST_CASE("Bridge handles SubAgentError -- updates card with error status") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(52, "agent-3", "fetch remote data"));

  bridge.HandleEvent(ChatEvent{
      .type = ChatEventType::SubAgentError,
      .message_id = 52,
      .sub_agent_id = "agent-3",
      .sub_agent_task = "fetch remote data",
      .sub_agent_result = "connection refused",
  });

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Error);
  const auto* call = ui.GetMessages()[0].ToolCall();
  REQUIRE(call != nullptr);
  const auto& sub = std::get<SubAgentCall>(*call);
  REQUIRE(sub.status == SubAgentStatus::Error);
  REQUIRE(sub.result == "connection refused");
}

TEST_CASE(
    "Bridge handles SubAgentCancelled -- updates card with cancelled "
    "status") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(53, "agent-4", "long running task"));

  bridge.HandleEvent(ChatEvent{
      .type = ChatEventType::SubAgentCancelled,
      .message_id = 53,
      .sub_agent_id = "agent-4",
      .sub_agent_task = "long running task",
  });

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Cancelled);
  const auto* call = ui.GetMessages()[0].ToolCall();
  REQUIRE(call != nullptr);
  const auto& sub = std::get<SubAgentCall>(*call);
  REQUIRE(sub.status == SubAgentStatus::Cancelled);
}

TEST_CASE("Bridge handles SubAgentProgress -- updates card with tool count") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(54, "agent-5", "progressive task"));

  bridge.HandleEvent(ChatEvent{
      .type = ChatEventType::SubAgentProgress,
      .message_id = 54,
      .sub_agent_id = "agent-5",
      .sub_agent_task = "progressive task",
      .sub_agent_tool_count = 3,
  });

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Active);
  const auto* call = ui.GetMessages()[0].ToolCall();
  REQUIRE(call != nullptr);
  const auto& sub = std::get<SubAgentCall>(*call);
  REQUIRE(sub.status == SubAgentStatus::Running);
  REQUIRE(sub.tool_count == 3);
}
