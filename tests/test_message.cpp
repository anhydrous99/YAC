#include "presentation/message.hpp"

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
  REQUIRE(msg.Text().empty());
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

TEST_CASE("SenderSwitch returns value for matching sender") {
  REQUIRE(SenderSwitch(
              Sender::User, [] { return 1; }, [] { return 2; },
              [] { return 3; }) == 1);
  REQUIRE(SenderSwitch(
              Sender::Agent, [] { return 1; }, [] { return 2; },
              [] { return 3; }) == 2);
  REQUIRE(SenderSwitch(
              Sender::Tool, [] { return 1; }, [] { return 2; },
              [] { return 3; }) == 3);
}

TEST_CASE("SenderSwitch uses explicit fallback for unknown sender") {
  const auto unknown_sender = static_cast<Sender>(99);

  REQUIRE(SenderSwitch(
              unknown_sender, [] { return 1; }, [] { return 2; },
              [] { return 3; }, [] { return 4; }) == 4);
}

TEST_CASE("SenderSwitch defaults unknown sender to tool branch") {
  const auto unknown_sender = static_cast<Sender>(99);

  REQUIRE(SenderSwitch(
              unknown_sender, [] { return 1; }, [] { return 2; },
              [] { return 3; }) == 3);
}

TEST_CASE("SenderSwitch evaluates only selected branch") {
  int user_calls = 0;
  int agent_calls = 0;
  int tool_calls = 0;
  int fallback_calls = 0;

  const int result = SenderSwitch(
      Sender::Agent,
      [&] {
        ++user_calls;
        return 1;
      },
      [&] {
        ++agent_calls;
        return 2;
      },
      [&] {
        ++tool_calls;
        return 3;
      },
      [&] {
        ++fallback_calls;
        return 4;
      });

  REQUIRE(result == 2);
  REQUIRE(user_calls == 0);
  REQUIRE(agent_calls == 1);
  REQUIRE(tool_calls == 0);
  REQUIRE(fallback_calls == 0);
}
