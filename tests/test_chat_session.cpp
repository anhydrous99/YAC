#include "presentation/chat_session.hpp"
#include "presentation/message_renderer.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation;

TEST_CASE("ChatSession stores text messages") {
  ChatSession session;

  session.AddMessage(Sender::User, "hello");
  session.AddMessage(Sender::Agent, "# response");

  REQUIRE(session.MessageCount() == 2);
  REQUIRE(session.Messages()[0].Text() == "hello");
  REQUIRE(session.Messages()[1].Text() == "# response");
}

TEST_CASE("ChatSession pre-parses agent markdown") {
  ChatSession session;

  session.AddMessage(Sender::Agent, "# response");

  REQUIRE(session.Messages()[0].render_cache.markdown_blocks.has_value());
  // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
  REQUIRE_FALSE(session.Messages()[0].render_cache.markdown_blocks->empty());
}

TEST_CASE("ChatSession stores tool messages with expansion state") {
  ChatSession session;

  session.AddToolCallMessage(
      tool_call::BashCall{"pwd", "/tmp/project", 0, false});

  REQUIRE(session.MessageCount() == 1);
  REQUIRE(session.Messages()[0].sender == Sender::Tool);
  REQUIRE(session.Messages()[0].ToolCall() != nullptr);
  REQUIRE(*session.ToolExpandedState(0));
}

TEST_CASE("ChatSession updates tool expansion state") {
  ChatSession session;
  session.AddToolCallMessage(tool_call::BashCall{"pwd", "/tmp", 0, false});

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
  REQUIRE(session.Messages()[0].render_cache.markdown_blocks.has_value());
}

TEST_CASE("ChatSession AppendToAgentMessage ignores unknown IDs") {
  ChatSession session;
  session.AddMessage(Sender::Agent, "existing");

  session.AppendToAgentMessage(999, "ignored");

  REQUIRE(session.MessageCount() == 1);
  REQUIRE(session.Messages()[0].Text() == "existing");
}

TEST_CASE("ChatSession status changes invalidate rendered element cache") {
  ChatSession session;

  auto id = session.AddMessage(Sender::Agent, "hello");
  (void)MessageRenderer::Render(session.Messages()[0], 80);

  REQUIRE(session.Messages()[0].render_cache.element.has_value());

  session.SetMessageStatus(id, MessageStatus::Active);

  REQUIRE_FALSE(session.Messages()[0].render_cache.element.has_value());
}

TEST_CASE("ChatSession status changes preserve parsed markdown cache") {
  ChatSession session;

  auto id = session.AddMessage(Sender::Agent, "# hello", MessageStatus::Active);

  REQUIRE(session.Messages()[0].render_cache.markdown_blocks.has_value());

  session.SetMessageStatus(id, MessageStatus::Complete);

  REQUIRE(session.Messages()[0].render_cache.markdown_blocks.has_value());
}
