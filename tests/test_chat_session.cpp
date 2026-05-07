#include "presentation/chat_session.hpp"
#include "tool_call/types.hpp"

#include <variant>

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation;
using namespace yac::tool_call;

TEST_CASE("ChatSession stores text messages") {
  ChatSession session;

  session.AddMessage(Sender::User, "hello");
  session.AddMessage(Sender::Agent, "# response");

  REQUIRE(session.MessageCount() == 2);
  REQUIRE(session.Messages()[0].CombinedText() == "hello");
  REQUIRE(session.Messages()[1].CombinedText() == "# response");
}

TEST_CASE("ChatSession AddToolCallSegment opens an agent turn") {
  ChatSession session;

  auto tool_id = session.AddToolCallSegment(BashCall{.command = "pwd",
                                                     .output = "/tmp/project",
                                                     .exit_code = 0,
                                                     .is_error = false},
                                            MessageStatus::Complete);

  REQUIRE(session.MessageCount() == 1);
  const auto& agent = session.Messages()[0];
  REQUIRE(agent.sender == Sender::Agent);
  REQUIRE(agent.segments.size() == 1);
  REQUIRE(std::holds_alternative<ToolSegment>(agent.segments[0]));
  REQUIRE(session.HasToolSegment(tool_id));
  REQUIRE_FALSE(*session.ToolExpandedState(tool_id));
}

TEST_CASE("ChatSession updates tool expansion state by id") {
  ChatSession session;
  auto tool_id = session.AddToolCallSegment(BashCall{
      .command = "pwd", .output = "/tmp", .exit_code = 0, .is_error = false});

  session.SetToolExpanded(tool_id, true);

  REQUIRE(*session.ToolExpandedState(tool_id));
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

  session.AddToolCallSegment(BashCall{
      .command = "ls", .output = "/tmp", .exit_code = 0, .is_error = false});
  auto after_tool = session.ContentGeneration();
  REQUIRE(after_tool > after_status);

  session.ClearMessages();
  auto after_clear = session.ContentGeneration();
  REQUIRE(after_clear > after_tool);
}

TEST_CASE(
    "ChatSession AppendToAgentMessage to an existing text segment does not "
    "bump PlanGeneration") {
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
  REQUIRE(session.Messages()[0].CombinedText() == "hello world");
}

TEST_CASE("ChatSession AppendToAgentMessage ignores unknown IDs") {
  ChatSession session;
  session.AddMessage(Sender::Agent, "existing");

  session.AppendToAgentMessage(999, "ignored");

  REQUIRE(session.MessageCount() == 1);
  REQUIRE(session.Messages()[0].CombinedText() == "existing");
}

TEST_CASE("ChatSession preserves emission order of text and tool segments") {
  ChatSession session;
  const auto agent_id =
      session.AddMessage(Sender::Agent, "", MessageStatus::Active);

  session.AppendToAgentMessage(agent_id, "before tool ");
  const auto tool_a = session.AddToolCallSegment(BashCall{
      .command = "ls", .output = "/", .exit_code = 0, .is_error = false});
  session.AppendToAgentMessage(agent_id, "between tools ");
  const auto tool_b = session.AddToolCallSegment(BashCall{
      .command = "pwd", .output = "/", .exit_code = 0, .is_error = false});
  session.AppendToAgentMessage(agent_id, "after tools");

  REQUIRE(session.MessageCount() == 1);
  const auto& agent = session.Messages()[0];
  REQUIRE(agent.segments.size() == 5);

  REQUIRE(std::holds_alternative<TextSegment>(agent.segments[0]));
  REQUIRE(std::get<TextSegment>(agent.segments[0]).text == "before tool ");

  REQUIRE(std::holds_alternative<ToolSegment>(agent.segments[1]));
  REQUIRE(std::get<ToolSegment>(agent.segments[1]).id == tool_a);

  REQUIRE(std::holds_alternative<TextSegment>(agent.segments[2]));
  REQUIRE(std::get<TextSegment>(agent.segments[2]).text == "between tools ");

  REQUIRE(std::holds_alternative<ToolSegment>(agent.segments[3]));
  REQUIRE(std::get<ToolSegment>(agent.segments[3]).id == tool_b);

  REQUIRE(std::holds_alternative<TextSegment>(agent.segments[4]));
  REQUIRE(std::get<TextSegment>(agent.segments[4]).text == "after tools");
}

TEST_CASE("ChatSession UpdateToolCallSegment updates an existing segment") {
  ChatSession session;
  const auto tool_id = session.AddToolCallSegment(BashCall{
      .command = "old", .output = "/", .exit_code = 0, .is_error = false});

  session.UpdateToolCallSegment(
      tool_id,
      BashCall{
          .command = "new", .output = "/", .exit_code = 0, .is_error = false},
      MessageStatus::Complete);

  const auto* segment = session.FindToolSegment(tool_id);
  REQUIRE(segment != nullptr);
  REQUIRE(std::holds_alternative<BashCall>(segment->block));
  REQUIRE(std::get<BashCall>(segment->block).command == "new");
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

  SECTION("AddToolCallSegment bumps") {
    session.AddToolCallSegment(BashCall{
        .command = "ls", .output = "/tmp", .exit_code = 0, .is_error = false});
    // EnsureAgentTurn opens a synthetic agent (one bump) then attaches the
    // tool segment (another bump).
    REQUIRE(session.PlanGeneration() >= gen0 + 1);
  }

  SECTION("AppendToAgentMessage to existing text segment does not bump") {
    auto id = session.AddMessage(Sender::Agent, "");
    const auto gen1 = session.PlanGeneration();
    session.AppendToAgentMessage(id, "hi");
    REQUIRE(session.PlanGeneration() == gen1);
    session.AppendToAgentMessage(id, " more");
    REQUIRE(session.PlanGeneration() == gen1);
    session.AppendToAgentMessage(id, "");
    REQUIRE(session.PlanGeneration() == gen1);
  }

  SECTION("AppendToAgentMessage opening a new text segment bumps") {
    auto id = session.AddMessage(Sender::Agent, "", MessageStatus::Active);
    session.AddToolCallSegment(BashCall{
        .command = "ls", .output = "/", .exit_code = 0, .is_error = false});
    const auto gen1 = session.PlanGeneration();
    session.AppendToAgentMessage(id, "after tool");
    REQUIRE(session.PlanGeneration() == gen1 + 1);
  }

  SECTION("SetMessageStatus bumps only when status changes") {
    auto id = session.AddMessage(Sender::Agent, "", MessageStatus::Active);
    const auto gen1 = session.PlanGeneration();
    session.SetMessageStatus(id, MessageStatus::Active);
    REQUIRE(session.PlanGeneration() == gen1);
    session.SetMessageStatus(id, MessageStatus::Complete);
    REQUIRE(session.PlanGeneration() == gen1 + 1);
  }

  SECTION("UpdateToolCallSegment bumps") {
    auto id = session.AddToolCallSegment(BashCall{
        .command = "ls", .output = "/tmp", .exit_code = 0, .is_error = false});
    const auto gen1 = session.PlanGeneration();
    session.UpdateToolCallSegment(id,
                                  BashCall{.command = "pwd",
                                           .output = "/home",
                                           .exit_code = 0,
                                           .is_error = false},
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
  auto parent = session.AddToolCallSegment(BashCall{
      .command = "cmd", .output = "/", .exit_code = 0, .is_error = false});
  const auto gen = session.PlanGeneration();

  (void)session.UpsertSubAgentToolCall(
      parent, ::yac::ToolCallId{"tool-1"}, "bash",
      BashCall{
          .command = "ls", .output = "/", .exit_code = 0, .is_error = false},
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

TEST_CASE("ChatSession AddToolCallSegment opens an Active agent when none") {
  ChatSession session;
  session.AddMessage(Sender::User, "hi", MessageStatus::Active);

  REQUIRE_FALSE(session.HasActiveAgent());
  session.AddToolCallSegment(BashCall{
      .command = "ls", .output = "/", .exit_code = 0, .is_error = false});
  REQUIRE(session.HasActiveAgent());
}
