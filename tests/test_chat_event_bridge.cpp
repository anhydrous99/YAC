#include "app/chat_event_bridge.hpp"
#include "presentation/chat_ui.hpp"
#include "tool_call/types.hpp"

#include <variant>

#include <catch2/catch_test_macros.hpp>

using namespace yac::app;
using namespace yac::chat;
using namespace yac::presentation;
using namespace yac::tool_call;

TEST_CASE("ChatEventBridge inserts queued user message by service ID") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(ChatEvent{.type = ChatEventType::UserMessageQueued,
                               .message_id = 10,
                               .role = ChatRole::User,
                               .text = "hello",
                               .status = ChatMessageStatus::Queued});

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].id == 10);
  REQUIRE(ui.GetMessages()[0].sender == Sender::User);
  REQUIRE(ui.GetMessages()[0].Text() == "hello");
}

TEST_CASE("ChatEventBridge streams assistant deltas by assistant service ID") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(ChatEvent{.type = ChatEventType::Started,
                               .message_id = 20,
                               .role = ChatRole::Assistant,
                               .status = ChatMessageStatus::Active});
  bridge.HandleEvent(ChatEvent{.type = ChatEventType::TextDelta,
                               .message_id = 20,
                               .role = ChatRole::Assistant,
                               .text = "partial"});
  bridge.HandleEvent(ChatEvent{.type = ChatEventType::AssistantMessageDone,
                               .message_id = 20,
                               .role = ChatRole::Assistant,
                               .status = ChatMessageStatus::Complete});

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].id == 20);
  REQUIRE(ui.GetMessages()[0].sender == Sender::Agent);
  REQUIRE(ui.GetMessages()[0].Text() == "partial");
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Complete);
  REQUIRE_FALSE(ui.IsTyping());
}

TEST_CASE("ChatEventBridge creates assistant error message when missing") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(ChatEvent{.type = ChatEventType::Error,
                               .message_id = 30,
                               .role = ChatRole::Assistant,
                               .text = "provider failed",
                               .status = ChatMessageStatus::Error});

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].id == 30);
  REQUIRE(ui.GetMessages()[0].sender == Sender::Agent);
  REQUIRE(ui.GetMessages()[0].Text() == "Error: provider failed");
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Error);
}

TEST_CASE("ChatEventBridge updates provider and model display") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(ChatEvent{.type = ChatEventType::ModelChanged,
                               .provider_id = "zai",
                               .model = "glm-5.1"});

  REQUIRE(ui.ProviderId() == "zai");
  REQUIRE(ui.Model() == "glm-5.1");
}

TEST_CASE("ChatEventBridge creates and updates tool call messages") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(
      ChatEvent{.type = ChatEventType::ToolCallStarted,
                .message_id = 40,
                .role = ChatRole::Tool,
                .tool_call = ListDirCall{"src", {}, false, false, ""},
                .status = ChatMessageStatus::Active});

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].id == 40);
  REQUIRE(ui.GetMessages()[0].sender == Sender::Tool);
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Active);

  bridge.HandleEvent(ChatEvent{
      .type = ChatEventType::ToolCallDone,
      .message_id = 40,
      .role = ChatRole::Tool,
      .tool_call = ListDirCall{"src",
                               {{"main.cpp", DirectoryEntryType::File, 10}},
                               false,
                               false,
                               ""},
      .status = ChatMessageStatus::Complete});

  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Complete);
  const auto* call = ui.GetMessages()[0].ToolCall();
  REQUIRE(call != nullptr);
  REQUIRE(std::get<ListDirCall>(*call).entries.size() == 1);
}
