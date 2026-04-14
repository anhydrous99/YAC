#include "app/chat_event_bridge.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::app;
using namespace yac::chat;
using namespace yac::presentation;

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
