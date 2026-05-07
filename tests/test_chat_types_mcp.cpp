#include "chat/types.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;

TEST_CASE("mcp_events_construct") {
  McpServerStateChangedEvent server_state;
  REQUIRE(server_state.server_id.value.empty());
  REQUIRE(server_state.state.empty());
  REQUIRE(server_state.error.empty());

  McpAuthRequiredEvent auth_required;
  REQUIRE(auth_required.server_id.value.empty());
  REQUIRE(auth_required.hint_message.empty());

  McpProgressUpdateEvent progress_update;
  REQUIRE(progress_update.message_id == 0);
  REQUIRE(progress_update.text.empty());
  REQUIRE(progress_update.progress == 0.0);
  REQUIRE(progress_update.total == 0.0);

  ChatEvent event{server_state};
  REQUIRE(event.Type() == ChatEventType::McpServerStateChanged);
  REQUIRE(event.As<McpServerStateChangedEvent>() != nullptr);
}
