#include "presentation/chat_ui.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>

using namespace yac::presentation;

namespace {

ftxui::Event MakeShiftEnter() {
  return ftxui::Event::Special("\x1b[13;2~");
}

ftxui::Event MakeCtrlEnter() {
  return ftxui::Event::Special("\x1b[13;5~");
}

ftxui::Event MakeKittyShiftEnter() {
  return ftxui::Event::Special("\x1b[27;2;13~");
}

ftxui::Event MakeKittyCtrlEnter() {
  return ftxui::Event::Special("\x1b[27;5;13~");
}

ftxui::Event MakeAltEnterCR() {
  return ftxui::Event::Special("\x1b\r");
}

ftxui::Event MakeAltEnterLF() {
  return ftxui::Event::Special("\x1b\n");
}

}  // namespace

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

TEST_CASE("AddMessage pre-parses markdown for agent messages") {
  ChatUI ui;
  ui.AddMessage(Sender::Agent, "# Hello\n\nSome **bold** text");

  const auto& msg = ui.GetMessages()[0];
  REQUIRE(msg.cached_blocks.has_value());
  REQUIRE_FALSE(msg.cached_blocks.value().empty());
}

TEST_CASE("AddMessage does not cache blocks for user messages") {
  ChatUI ui;
  ui.AddMessage(Sender::User, "plain text");

  const auto& msg = ui.GetMessages()[0];
  REQUIRE_FALSE(msg.cached_blocks.has_value());
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

TEST_CASE("SetTyping toggles state") {
  ChatUI ui;
  REQUIRE_FALSE(ui.IsTyping());

  ui.SetTyping(true);
  REQUIRE(ui.IsTyping());

  ui.SetTyping(false);
  REQUIRE_FALSE(ui.IsTyping());
}

TEST_CASE("Build returns non-null component with typing enabled") {
  ChatUI ui;
  ui.SetTyping(true);
  auto component = ui.Build();
  REQUIRE(component != nullptr);
}

TEST_CASE("CalculateInputHeight returns 1 for empty input") {
  ChatUI ui;
  REQUIRE(ui.CalculateInputHeight() == 1);
}

TEST_CASE("CalculateInputHeight grows with newlines") {
  ChatUI ui;
  (void)ui.HandleInputEvent(MakeShiftEnter());
  (void)ui.HandleInputEvent(MakeShiftEnter());
  REQUIRE(ui.CalculateInputHeight() == 3);
}

TEST_CASE("CalculateInputHeight caps at kMaxInputLines") {
  ChatUI ui;
  for (int i = 0; i < ChatUI::kMaxInputLines + 5; ++i) {
    (void)ui.HandleInputEvent(MakeShiftEnter());
  }
  REQUIRE(ui.CalculateInputHeight() == ChatUI::kMaxInputLines);
}

TEST_CASE("HandleInputEvent returns true for plain Enter") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(ftxui::Event::Return));
}

TEST_CASE("HandleInputEvent returns false for regular character") {
  ChatUI ui;
  REQUIRE_FALSE(ui.HandleInputEvent(ftxui::Event::Character('a')));
}

TEST_CASE("HandleInputEvent returns false for mouse events") {
  ChatUI ui;
  auto event = ftxui::Event::Mouse("", ftxui::Mouse{});
  REQUIRE_FALSE(ui.HandleInputEvent(event));
}

TEST_CASE("HandleInputEvent inserts newline on Shift+Enter escape") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeShiftEnter()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent inserts newline on Ctrl+Enter escape") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeCtrlEnter()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent inserts newline on kitty Shift+Enter") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeKittyShiftEnter()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent inserts newline on kitty Ctrl+Enter") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeKittyCtrlEnter()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent accumulates newlines") {
  ChatUI ui;
  (void)ui.HandleInputEvent(MakeShiftEnter());
  (void)ui.HandleInputEvent(MakeCtrlEnter());
  (void)ui.HandleInputEvent(MakeKittyShiftEnter());
  REQUIRE(ui.CalculateInputHeight() == 4);
}

TEST_CASE("HandleInputEvent inserts newline on Alt+Enter (CR variant)") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeAltEnterCR()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent inserts newline on Alt+Enter (LF variant)") {
  ChatUI ui;
  REQUIRE(ui.HandleInputEvent(MakeAltEnterLF()));
  REQUIRE(ui.CalculateInputHeight() == 2);
}

TEST_CASE("HandleInputEvent Enter submits and clears content") {
  bool sent = false;
  std::string captured;
  ChatUI ui([&](const std::string& msg) {
    sent = true;
    captured = msg;
  });

  (void)ui.HandleInputEvent(ftxui::Event::Return);
  REQUIRE_FALSE(sent);

  (void)ui.HandleInputEvent(MakeAltEnterCR());
  REQUIRE(ui.HandleInputEvent(ftxui::Event::Return));
  REQUIRE(sent);
  REQUIRE(captured == "\n");
  REQUIRE(ui.CalculateInputHeight() == 1);
}
