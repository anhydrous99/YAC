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

  bridge.HandleEvent(
      ChatEvent{UserMessageQueuedEvent{.message_id = 10,
                                       .role = ChatRole::User,
                                       .text = "hello",
                                       .status = ChatMessageStatus::Queued}});

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].id == 10);
  REQUIRE(ui.GetMessages()[0].sender == Sender::User);
  REQUIRE(ui.GetMessages()[0].CombinedText() == "hello");
}

TEST_CASE("ChatEventBridge streams assistant deltas by assistant service ID") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(
      ChatEvent{StartedEvent{.message_id = 20,
                             .role = ChatRole::Assistant,
                             .status = ChatMessageStatus::Active}});
  bridge.HandleEvent(ChatEvent{TextDeltaEvent{
      .message_id = 20, .role = ChatRole::Assistant, .text = "partial"}});
  bridge.HandleEvent(ChatEvent{
      AssistantMessageDoneEvent{.message_id = 20,
                                .role = ChatRole::Assistant,
                                .status = ChatMessageStatus::Complete}});

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].id == 20);
  REQUIRE(ui.GetMessages()[0].sender == Sender::Agent);
  REQUIRE(ui.GetMessages()[0].CombinedText() == "partial");
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Complete);
  REQUIRE_FALSE(ui.IsTyping());
}

TEST_CASE("ChatEventBridge creates assistant error message when missing") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(ChatEvent{ErrorEvent{.message_id = 30,
                                          .role = ChatRole::Assistant,
                                          .text = "provider failed",
                                          .status = ChatMessageStatus::Error}});

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].id == 30);
  REQUIRE(ui.GetMessages()[0].sender == Sender::Agent);
  REQUIRE(ui.GetMessages()[0].CombinedText() == "Error: provider failed");
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Error);
}

TEST_CASE("ChatEventBridge updates provider and model display") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(
      ChatEvent{ModelChangedEvent{.provider_id = "zai", .model = "glm-5.1"}});

  REQUIRE(ui.ProviderId() == "zai");
  REQUIRE(ui.Model() == "glm-5.1");
}

TEST_CASE("ChatEventBridge creates and updates tool call messages") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(ChatEvent{ToolCallStartedEvent{
      .message_id = 40,
      .role = ChatRole::Tool,
      .tool_call = ListDirCall{"src", {}, false, false, ""},
      .status = ChatMessageStatus::Active}});

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].sender == Sender::Agent);
  const auto* tool = ui.GetMessages()[0].FindToolSegment(40);
  REQUIRE(tool != nullptr);
  REQUIRE(tool->status == MessageStatus::Active);

  bridge.HandleEvent(ChatEvent{ToolCallDoneEvent{
      .message_id = 40,
      .role = ChatRole::Tool,
      .tool_call = ListDirCall{"src",
                               {{"main.cpp", DirectoryEntryType::File, 10}},
                               false,
                               false,
                               ""},
      .status = ChatMessageStatus::Complete}});

  tool = ui.GetMessages()[0].FindToolSegment(40);
  REQUIRE(tool != nullptr);
  REQUIRE(tool->status == MessageStatus::Complete);
  REQUIRE(std::get<ListDirCall>(tool->block).entries.size() == 1);
}

TEST_CASE("ChatEvent reports payload type for each event family") {
  REQUIRE(ChatEvent{StartedEvent{}}.Type() == ChatEventType::Started);
  REQUIRE(ChatEvent{TextDeltaEvent{}}.Type() == ChatEventType::TextDelta);
  REQUIRE(ChatEvent{ToolCallStartedEvent{}}.Type() ==
          ChatEventType::ToolCallStarted);
  REQUIRE(ChatEvent{ModelChangedEvent{}}.Type() == ChatEventType::ModelChanged);
  REQUIRE(ChatEvent{SubAgentCompletedEvent{}}.Type() ==
          ChatEventType::SubAgentCompleted);
}
