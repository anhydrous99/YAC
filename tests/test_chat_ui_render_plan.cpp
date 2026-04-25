#include "presentation/chat_ui_render_plan.hpp"
#include "presentation/message.hpp"
#include "tool_call/types.hpp"

#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation;
using namespace yac::tool_call;

TEST_CASE(
    "BuildMessageRenderPlan emits a 1:1 user/agent plan in original order") {
  std::vector<Message> messages;
  messages.push_back(Message(Sender::User, "hi"));
  messages.push_back(Message(Sender::Agent, "answer"));
  messages.push_back(Message(Sender::User, "thanks"));
  messages.push_back(Message(Sender::Agent, "you're welcome"));

  const auto plan = BuildMessageRenderPlan(messages);

  REQUIRE(plan.size() == 4);
  REQUIRE(plan[0].kind == MessageRenderItem::Kind::User);
  REQUIRE(plan[0].message_index == 0);
  REQUIRE(plan[1].kind == MessageRenderItem::Kind::Agent);
  REQUIRE(plan[1].message_index == 1);
  REQUIRE(plan[2].kind == MessageRenderItem::Kind::User);
  REQUIRE(plan[2].message_index == 2);
  REQUIRE(plan[3].kind == MessageRenderItem::Kind::Agent);
  REQUIRE(plan[3].message_index == 3);
}

TEST_CASE(
    "BuildMessageRenderPlan reflects an agent message regardless of its "
    "segment composition") {
  Message agent(Sender::Agent, "leading text");
  agent.segments.emplace_back(
      ToolSegment{99, BashCall{"ls", "", 0, false}, MessageStatus::Complete});
  agent.AppendText("trailing");

  std::vector<Message> messages;
  messages.push_back(std::move(agent));

  const auto plan = BuildMessageRenderPlan(messages);

  REQUIRE(plan.size() == 1);
  REQUIRE(plan[0].kind == MessageRenderItem::Kind::Agent);
  REQUIRE(plan[0].message_index == 0);
}
