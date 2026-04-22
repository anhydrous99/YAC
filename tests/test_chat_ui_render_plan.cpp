#include "presentation/chat_ui_render_plan.hpp"
#include "presentation/message.hpp"
#include "tool_call/types.hpp"

#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation;
using namespace yac::tool_call;

namespace {

Message AgentMessage(std::string text) {
  return Message(Sender::Agent, std::move(text));
}

Message UserMessage(std::string text) {
  return Message(Sender::User, std::move(text));
}

Message ToolMessage(ToolCallBlock block,
                    MessageStatus status = MessageStatus::Complete) {
  auto message = Message::Tool(std::move(block));
  message.status = status;
  return message;
}

}  // namespace

TEST_CASE(
    "BuildMessageRenderPlan groups consecutive tool messages after an agent "
    "message") {
  std::vector<Message> messages;
  messages.push_back(AgentMessage("first answer"));
  messages.push_back(ToolMessage(BashCall{"ls", "", 0, false}));
  messages.push_back(ToolMessage(FileReadCall{"README.md", 8, "preview"},
                                 MessageStatus::Active));
  messages.push_back(UserMessage("thanks"));
  messages.push_back(ToolMessage(BashCall{"pwd", "", 0, false}));

  const auto plan = BuildMessageRenderPlan(messages);

  REQUIRE(plan.size() == 3);

  REQUIRE(plan[0].kind == MessageRenderItem::Kind::AgentGroup);
  REQUIRE(plan[0].message_index == 0);
  REQUIRE(plan[0].group_ordinal == 0);
  REQUIRE(plan[0].any_tool_active);
  REQUIRE(plan[0].tools.size() == 2);
  REQUIRE(plan[0].tools[0] == ToolRenderRef{1, 0});
  REQUIRE(plan[0].tools[1] == ToolRenderRef{2, 1});

  REQUIRE(plan[1].kind == MessageRenderItem::Kind::StandaloneMessage);
  REQUIRE(plan[1].message_index == 3);

  REQUIRE(plan[2].kind == MessageRenderItem::Kind::StandaloneTool);
  REQUIRE(plan[2].message_index == 4);
  REQUIRE(plan[2].tool_state_index == 2);
}

TEST_CASE(
    "BuildMessageRenderPlan increments group ordinals across agent runs") {
  std::vector<Message> messages;
  messages.push_back(AgentMessage("first answer"));
  messages.push_back(ToolMessage(BashCall{"git status", "", 0, false}));
  messages.push_back(AgentMessage("second answer"));
  messages.push_back(ToolMessage(BashCall{"git diff", "", 0, false}));
  messages.push_back(ToolMessage(FileReadCall{"src/main.cpp", 20, "main"}));
  messages.push_back(AgentMessage("final answer"));

  const auto plan = BuildMessageRenderPlan(messages);

  REQUIRE(plan.size() == 3);

  REQUIRE(plan[0].kind == MessageRenderItem::Kind::AgentGroup);
  REQUIRE(plan[0].group_ordinal == 0);
  REQUIRE_FALSE(plan[0].any_tool_active);
  REQUIRE(plan[0].tools.size() == 1);
  REQUIRE(plan[0].tools[0] == ToolRenderRef{1, 0});

  REQUIRE(plan[1].kind == MessageRenderItem::Kind::AgentGroup);
  REQUIRE(plan[1].message_index == 2);
  REQUIRE(plan[1].group_ordinal == 1);
  REQUIRE(plan[1].tools.size() == 2);
  REQUIRE(plan[1].tools[0] == ToolRenderRef{3, 1});
  REQUIRE(plan[1].tools[1] == ToolRenderRef{4, 2});

  REQUIRE(plan[2].kind == MessageRenderItem::Kind::StandaloneMessage);
  REQUIRE(plan[2].message_index == 5);
}
