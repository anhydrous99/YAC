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
  REQUIRE(msg.content.empty());
  REQUIRE(msg.DisplayLabel() == "You");
}
