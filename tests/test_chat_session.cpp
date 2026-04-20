#include "presentation/chat_session.hpp"
#include "tool_call/types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation;
using namespace yac::tool_call;

TEST_CASE("ChatSession stores text messages") {
  ChatSession session;

  session.AddMessage(Sender::User, "hello");
  session.AddMessage(Sender::Agent, "# response");

  REQUIRE(session.MessageCount() == 2);
  REQUIRE(session.Messages()[0].Text() == "hello");
  REQUIRE(session.Messages()[1].Text() == "# response");
}

TEST_CASE("ChatSession stores tool messages with expansion state") {
  ChatSession session;

  session.AddToolCallMessage(BashCall{"pwd", "/tmp/project", 0, false});

  REQUIRE(session.MessageCount() == 1);
  REQUIRE(session.Messages()[0].sender == Sender::Tool);
  REQUIRE(session.Messages()[0].ToolCall() != nullptr);
  REQUIRE_FALSE(*session.ToolExpandedState(0));
}

TEST_CASE("ChatSession updates tool expansion state") {
  ChatSession session;
  session.AddToolCallMessage(BashCall{"pwd", "/tmp", 0, false});

  session.SetToolExpanded(0, false);

  REQUIRE_FALSE(*session.ToolExpandedState(0));
}

TEST_CASE("ChatSession appends streaming deltas to agent message by ID") {
  ChatSession session;

  auto id = session.AddMessage(Sender::Agent, "");
  session.AppendToAgentMessage(id, "hello");
  session.AppendToAgentMessage(id, " world");

  REQUIRE(session.MessageCount() == 1);
  REQUIRE(session.Messages()[0].sender == Sender::Agent);
  REQUIRE(session.Messages()[0].Text() == "hello world");
}

TEST_CASE("ChatSession AppendToAgentMessage ignores unknown IDs") {
  ChatSession session;
  session.AddMessage(Sender::Agent, "existing");

  session.AppendToAgentMessage(999, "ignored");

  REQUIRE(session.MessageCount() == 1);
  REQUIRE(session.Messages()[0].Text() == "existing");
}

TEST_CASE("ChatSession inserts explicit message IDs") {
  ChatSession session;

  auto id = session.AddMessageWithId(42, Sender::Agent, "hello");

  REQUIRE(id == 42);
  REQUIRE(session.HasMessage(42));
  REQUIRE(session.Messages()[0].id == 42);
}

TEST_CASE("ChatSession honors role_label override for user messages") {
  ChatSession session;

  session.AddMessageWithId(7, Sender::User, "continuation body",
                           MessageStatus::Complete, "Sub-agent");

  REQUIRE(session.MessageCount() == 1);
  REQUIRE(session.Messages()[0].sender == Sender::User);
  REQUIRE(session.Messages()[0].DisplayLabel() == "Sub-agent");
}
