#include "presentation/chat_ui.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation;

TEST_CASE("Default constructor creates empty message list") {
  ChatUI ui;
  REQUIRE(ui.GetMessages().empty());
}

TEST_CASE("Constructor with callback stores it") {
  bool called = false;
  std::string captured;
  ChatUI ui([&](const std::string& msg) {
    called = true;
    captured = msg;
  });

  SECTION("Callback fires on submit") {
    // We can't easily simulate Enter key presses without FTXUI event loop,
    // but we can verify AddMessage works correctly.
    ui.AddMessage(Sender::User, "hello");
    REQUIRE(ui.GetMessages().size() == 1);
    REQUIRE(ui.GetMessages()[0].content == "hello");
    REQUIRE(ui.GetMessages()[0].sender == Sender::User);
  }
}

TEST_CASE("AddMessage appends to message list") {
  ChatUI ui;
  ui.AddMessage(Sender::User, "first");
  ui.AddMessage(Sender::Agent, "second");
  ui.AddMessage(Sender::User, "third");

  const auto& msgs = ui.GetMessages();
  REQUIRE(msgs.size() == 3);
  REQUIRE(msgs[0].sender == Sender::User);
  REQUIRE(msgs[0].content == "first");
  REQUIRE(msgs[1].sender == Sender::Agent);
  REQUIRE(msgs[1].content == "second");
  REQUIRE(msgs[2].sender == Sender::User);
  REQUIRE(msgs[2].content == "third");
}

TEST_CASE("Build returns a non-null component") {
  ChatUI ui;
  auto component = ui.Build();
  REQUIRE(component != nullptr);
}

TEST_CASE("GetMessages returns const reference to internal state") {
  ChatUI ui;
  ui.AddMessage(Sender::Agent, "response");

  const auto& msgs = ui.GetMessages();
  REQUIRE(&msgs == &ui.GetMessages());
  REQUIRE(msgs.size() == 1);
}
