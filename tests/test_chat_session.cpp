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

TEST_CASE("ChatSession ContentGeneration bumps on every content mutation") {
  ChatSession session;
  auto base = session.ContentGeneration();

  const auto id = session.AddMessage(Sender::Agent, "", MessageStatus::Active);
  auto after_add = session.ContentGeneration();
  REQUIRE(after_add > base);

  session.AppendToAgentMessage(id, "hello");
  auto after_append = session.ContentGeneration();
  REQUIRE(after_append > after_add);

  session.SetMessageStatus(id, MessageStatus::Complete);
  auto after_status = session.ContentGeneration();
  REQUIRE(after_status > after_append);

  session.AddToolCallMessage(BashCall{"ls", "/tmp", 0, false});
  auto after_tool = session.ContentGeneration();
  REQUIRE(after_tool > after_status);

  session.ClearMessages();
  auto after_clear = session.ContentGeneration();
  REQUIRE(after_clear > after_tool);
}

TEST_CASE(
    "ChatSession AppendToAgentMessage bumps ContentGeneration without "
    "bumping PlanGeneration") {
  ChatSession session;
  const auto id = session.AddMessage(Sender::Agent, "", MessageStatus::Active);

  auto plan_before = session.PlanGeneration();
  auto content_before = session.ContentGeneration();

  session.AppendToAgentMessage(id, "token");

  REQUIRE(session.PlanGeneration() == plan_before);
  REQUIRE(session.ContentGeneration() > content_before);
}

TEST_CASE("ChatSession AppendToAgentMessage with empty delta is a no-op") {
  ChatSession session;
  const auto id = session.AddMessage(Sender::Agent, "seed");
  auto content_before = session.ContentGeneration();

  session.AppendToAgentMessage(id, "");

  REQUIRE(session.ContentGeneration() == content_before);
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

TEST_CASE("ChatSession bumps PlanGeneration on plan-shape mutators") {
  ChatSession session;
  const auto gen0 = session.PlanGeneration();

  SECTION("AddMessage bumps") {
    session.AddMessage(Sender::User, "a");
    REQUIRE(session.PlanGeneration() == gen0 + 1);
  }

  SECTION("AddMessageWithId bumps") {
    session.AddMessageWithId(10, Sender::Agent, "x");
    REQUIRE(session.PlanGeneration() == gen0 + 1);
  }

  SECTION("AddToolCallMessage bumps") {
    session.AddToolCallMessage(BashCall{"ls", "/tmp", 0, false});
    REQUIRE(session.PlanGeneration() == gen0 + 1);
  }

  SECTION("AppendToAgentMessage does NOT bump plan generation") {
    auto id = session.AddMessage(Sender::Agent, "");
    const auto gen1 = session.PlanGeneration();
    session.AppendToAgentMessage(id, "hi");
    REQUIRE(session.PlanGeneration() == gen1);
    session.AppendToAgentMessage(id, " more");
    REQUIRE(session.PlanGeneration() == gen1);
    session.AppendToAgentMessage(id, "");
    REQUIRE(session.PlanGeneration() == gen1);
  }

  SECTION("SetMessageStatus bumps only when status changes") {
    auto id = session.AddMessage(Sender::Agent, "", MessageStatus::Active);
    const auto gen1 = session.PlanGeneration();
    session.SetMessageStatus(id, MessageStatus::Active);
    REQUIRE(session.PlanGeneration() == gen1);
    session.SetMessageStatus(id, MessageStatus::Complete);
    REQUIRE(session.PlanGeneration() == gen1 + 1);
  }

  SECTION("SetToolCallMessage bumps") {
    auto id = session.AddToolCallMessage(BashCall{"ls", "/tmp", 0, false});
    const auto gen1 = session.PlanGeneration();
    session.SetToolCallMessage(id, BashCall{"pwd", "/home", 0, false},
                               MessageStatus::Complete);
    REQUIRE(session.PlanGeneration() == gen1 + 1);
  }

  SECTION("ClearMessages bumps") {
    session.AddMessage(Sender::User, "a");
    const auto gen1 = session.PlanGeneration();
    session.ClearMessages();
    REQUIRE(session.PlanGeneration() == gen1 + 1);
  }
}

TEST_CASE("ChatSession does not bump generation on read accessors") {
  ChatSession session;
  session.AddMessage(Sender::User, "hello");
  const auto gen = session.PlanGeneration();

  (void)session.Messages();
  (void)session.MessageCount();
  (void)session.Empty();
  (void)session.HasMessage(1);
  (void)session.FindMessageIndex(1);

  REQUIRE(session.PlanGeneration() == gen);
}

TEST_CASE("ChatSession UpsertSubAgentToolCall does not bump generation") {
  ChatSession session;
  auto parent = session.AddToolCallMessage(BashCall{"cmd", "/", 0, false});
  const auto gen = session.PlanGeneration();

  (void)session.UpsertSubAgentToolCall(parent, "tool-1", "bash",
                                       BashCall{"ls", "/", 0, false},
                                       MessageStatus::Complete);

  REQUIRE(session.PlanGeneration() == gen);
}

TEST_CASE("ChatSession FindMessageIndex returns correct index for many IDs") {
  ChatSession session;
  std::vector<MessageId> ids;
  for (int i = 0; i < 200; ++i) {
    ids.push_back(session.AddMessage(Sender::User, "msg " + std::to_string(i)));
  }

  for (size_t i = 0; i < ids.size(); ++i) {
    auto idx = session.FindMessageIndex(ids[i]);
    REQUIRE(idx.has_value());
    REQUIRE(idx.value() == i);
  }
  REQUIRE_FALSE(session.FindMessageIndex(999999).has_value());
  REQUIRE(session.HasMessage(ids.front()));
  REQUIRE(session.HasMessage(ids.back()));
  REQUIRE_FALSE(session.HasMessage(0));
}

TEST_CASE("ChatSession FindMessageIndex is cleared by ClearMessages") {
  ChatSession session;
  auto id = session.AddMessage(Sender::User, "hi");
  REQUIRE(session.FindMessageIndex(id).has_value());

  session.ClearMessages();

  REQUIRE_FALSE(session.FindMessageIndex(id).has_value());
  REQUIRE_FALSE(session.HasMessage(id));
}

TEST_CASE("ChatSession HasActiveAgent tracks add/status/clear transitions") {
  ChatSession session;
  REQUIRE_FALSE(session.HasActiveAgent());

  auto user_id = session.AddMessage(Sender::User, "hi");
  REQUIRE_FALSE(session.HasActiveAgent());
  (void)user_id;

  auto agent_id = session.AddMessage(Sender::Agent, "", MessageStatus::Active);
  REQUIRE(session.HasActiveAgent());

  auto agent_id2 = session.AddMessage(Sender::Agent, "", MessageStatus::Active);
  REQUIRE(session.HasActiveAgent());

  session.SetMessageStatus(agent_id, MessageStatus::Complete);
  REQUIRE(session.HasActiveAgent());
  session.SetMessageStatus(agent_id2, MessageStatus::Complete);
  REQUIRE_FALSE(session.HasActiveAgent());

  session.SetMessageStatus(agent_id2, MessageStatus::Active);
  REQUIRE(session.HasActiveAgent());

  session.ClearMessages();
  REQUIRE_FALSE(session.HasActiveAgent());
}

TEST_CASE("ChatSession HasActiveAgent ignores non-agent messages") {
  ChatSession session;

  session.AddMessage(Sender::User, "hi", MessageStatus::Active);
  session.AddToolCallMessage(BashCall{"ls", "/", 0, false});

  REQUIRE_FALSE(session.HasActiveAgent());
}
