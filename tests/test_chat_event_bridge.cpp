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
      ChatEvent{ModelChangedEvent{.provider_id = ::yac::ProviderId{"zai"},
                                  .model = ::yac::ModelId{"glm-5.1"}}});

  REQUIRE(ui.ProviderId() == "zai");
  REQUIRE(ui.Model() == "glm-5.1");
}

TEST_CASE(
    "ChatEventBridge resolves context window via injected resolver, "
    "overriding the cross-provider table") {
  ChatUI ui;
  // gpt-4o-mini's cross-provider table value is 128000; the resolver returns
  // 64000. The resolver wins.
  ChatEventBridge bridge(ui, /*history_provider=*/{},
                         [](const std::string&) { return 64000; });

  bridge.HandleEvent(
      ChatEvent{ModelChangedEvent{.provider_id = ::yac::ProviderId{"openai"},
                                  .model = ::yac::ModelId{"gpt-4o-mini"}}});

  REQUIRE(ui.ContextWindowTokens() == 64000);
}

TEST_CASE(
    "ChatEventBridge falls back to LookupContextWindow when resolver is "
    "unset") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(
      ChatEvent{ModelChangedEvent{.provider_id = ::yac::ProviderId{"openai"},
                                  .model = ::yac::ModelId{"gpt-4o-mini"}}});

  REQUIRE(ui.ContextWindowTokens() == 128000);
}

TEST_CASE("ChatEventBridge creates and updates tool call messages") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(ChatEvent{
      ToolCallStartedEvent{.message_id = 40,
                           .role = ChatRole::Tool,
                           .tool_call = ListDirCall{.path = "src",
                                                    .entries = {},
                                                    .truncated = false,
                                                    .is_error = false,
                                                    .error = ""},
                           .status = ChatMessageStatus::Active}});

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].sender == Sender::Agent);
  const auto* tool = ui.GetMessages()[0].FindToolSegment(40);
  REQUIRE(tool != nullptr);
  REQUIRE(tool->status == MessageStatus::Active);

  bridge.HandleEvent(ChatEvent{ToolCallDoneEvent{
      .message_id = 40,
      .role = ChatRole::Tool,
      .tool_call =
          ListDirCall{.path = "src",
                      .entries = {{"main.cpp", DirectoryEntryType::File, 10}},
                      .truncated = false,
                      .is_error = false,
                      .error = ""},
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

TEST_CASE("ChatEventBridge appends a notice on ModelChangedEvent") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(
      ChatEvent{ModelChangedEvent{.provider_id = ::yac::ProviderId{"openai"},
                                  .model = ::yac::ModelId{"gpt-4o-mini"}}});

  REQUIRE(ui.GetNotices().size() == 1);
  REQUIRE(ui.GetNotices()[0].notice.title == "Model switched");
}

TEST_CASE("ChatEventBridge appends a notice on ConversationCompactedEvent") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(ChatEvent{ConversationCompactedEvent{
      .reason = CompactReason::Auto, .messages_removed = 3}});

  REQUIRE(ui.GetNotices().size() == 1);
  REQUIRE(ui.GetNotices()[0].notice.title.find(
              "Auto-compacted near context limit") != std::string::npos);
}

TEST_CASE(
    "ChatEventBridge does not append a notice when ErrorEvent has a "
    "matching message") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(
      ChatEvent{StartedEvent{.message_id = 50,
                             .role = ChatRole::Assistant,
                             .status = ChatMessageStatus::Active}});
  bridge.HandleEvent(ChatEvent{ErrorEvent{.message_id = 50,
                                          .role = ChatRole::Assistant,
                                          .text = "provider failed",
                                          .status = ChatMessageStatus::Error}});

  REQUIRE(ui.GetNotices().empty());
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Error);
}

TEST_CASE("ChatEventBridge does not append a notice on CancelledEvent") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(
      ChatEvent{StartedEvent{.message_id = 60,
                             .role = ChatRole::Assistant,
                             .status = ChatMessageStatus::Active}});
  bridge.HandleEvent(ChatEvent{CancelledEvent{.message_id = 60}});

  REQUIRE(ui.GetNotices().empty());
  REQUIRE(ui.GetMessages()[0].status == MessageStatus::Cancelled);
}

TEST_CASE(
    "ChatEventBridge does not append a notice on ConversationClearedEvent") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(
      ChatEvent{StartedEvent{.message_id = 70,
                             .role = ChatRole::Assistant,
                             .status = ChatMessageStatus::Active}});
  bridge.HandleEvent(ChatEvent{ConversationClearedEvent{}});

  REQUIRE(ui.GetMessages().empty());
  REQUIRE(ui.GetNotices().empty());
}
