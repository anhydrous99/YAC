#include "presentation/message.hpp"

#include <variant>

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation;

TEST_CASE("DisplayLabel returns custom role_label when set") {
  Message msg{Sender::User, "hi", "Bot", ""};
  REQUIRE(msg.DisplayLabel() == "Bot");
}

TEST_CASE("DisplayLabel returns You for User without custom label") {
  Message msg{Sender::User, "hi"};
  REQUIRE(msg.DisplayLabel() == "You");
}

TEST_CASE("DisplayLabel returns Assistant for Agent without custom label") {
  Message msg{Sender::Agent, "hi"};
  REQUIRE(msg.DisplayLabel() == "Assistant");
}

TEST_CASE("Default-constructed Message is User with empty content") {
  Message msg;
  REQUIRE(msg.sender == Sender::User);
  REQUIRE(msg.CombinedText().empty());
  REQUIRE(msg.DisplayLabel() == "You");
}

TEST_CASE("Aggregate init Message has non-zero created_at") {
  Message msg{Sender::User, "hello"};
  REQUIRE(msg.created_at != std::chrono::system_clock::time_point{});
}

TEST_CASE("Default-constructed Message has non-zero created_at") {
  Message msg;
  REQUIRE(msg.created_at != std::chrono::system_clock::time_point{});
}

TEST_CASE("AppendText opens a new text segment after a tool segment") {
  Message msg{Sender::Agent, "first"};
  msg.segments.emplace_back(ToolSegment{42, ::yac::tool_call::ToolCallBlock{},
                                        MessageStatus::Complete});
  msg.AppendText("second");

  REQUIRE(msg.segments.size() == 3);
  REQUIRE(std::holds_alternative<TextSegment>(msg.segments[0]));
  REQUIRE(std::get<TextSegment>(msg.segments[0]).text == "first");
  REQUIRE(std::holds_alternative<ToolSegment>(msg.segments[1]));
  REQUIRE(std::holds_alternative<TextSegment>(msg.segments[2]));
  REQUIRE(std::get<TextSegment>(msg.segments[2]).text == "second");
}

TEST_CASE("AppendText extends the trailing text segment when present") {
  Message msg{Sender::Agent, "hello"};
  msg.AppendText(" world");

  REQUIRE(msg.segments.size() == 1);
  REQUIRE(std::get<TextSegment>(msg.segments[0]).text == "hello world");
}

TEST_CASE("CombinedText concatenates every text segment in order") {
  Message msg{Sender::Agent, "alpha"};
  msg.segments.emplace_back(ToolSegment{1, ::yac::tool_call::ToolCallBlock{},
                                        MessageStatus::Complete});
  msg.AppendText("beta");

  REQUIRE(msg.CombinedText() == "alphabeta");
}

TEST_CASE("FindToolSegment locates a segment by id") {
  Message msg{Sender::Agent, ""};
  msg.segments.emplace_back(ToolSegment{7, ::yac::tool_call::ToolCallBlock{},
                                        MessageStatus::Complete});
  msg.segments.emplace_back(ToolSegment{8, ::yac::tool_call::ToolCallBlock{},
                                        MessageStatus::Complete});

  REQUIRE(msg.FindToolSegment(8) != nullptr);
  REQUIRE(msg.FindToolSegment(7)->id == 7);
  REQUIRE(msg.FindToolSegment(99) == nullptr);
}

TEST_CASE("SenderSwitch returns value for matching sender") {
  REQUIRE(SenderSwitch(Sender::User, [] { return 1; }, [] { return 2; }) == 1);
  REQUIRE(SenderSwitch(Sender::Agent, [] { return 1; }, [] { return 2; }) == 2);
}

TEST_CASE("SenderSwitch evaluates only selected branch") {
  int user_calls = 0;
  int agent_calls = 0;

  const int result = SenderSwitch(
      Sender::Agent,
      [&] {
        ++user_calls;
        return 1;
      },
      [&] {
        ++agent_calls;
        return 2;
      });

  REQUIRE(result == 2);
  REQUIRE(user_calls == 0);
  REQUIRE(agent_calls == 1);
}
