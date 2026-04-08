#include "presentation/markdown/parser.hpp"
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

TEST_CASE("Default-constructed Message has no cached_blocks") {
  Message msg;
  REQUIRE_FALSE(msg.cached_blocks.has_value());
}

TEST_CASE("cached_blocks can be set and read") {
  Message msg{Sender::Agent, "# hello"};
  REQUIRE_FALSE(msg.cached_blocks.has_value());

  msg.cached_blocks = markdown::MarkdownParser::Parse(msg.content);
  REQUIRE(msg.cached_blocks.has_value());
  REQUIRE_FALSE(msg.cached_blocks->empty());
}

TEST_CASE("Aggregate init Message has non-zero created_at") {
  Message msg{Sender::User, "hello"};
  REQUIRE(msg.created_at != std::chrono::system_clock::time_point{});
}

TEST_CASE("Default-constructed Message has non-zero created_at") {
  Message msg;
  REQUIRE(msg.created_at != std::chrono::system_clock::time_point{});
}
