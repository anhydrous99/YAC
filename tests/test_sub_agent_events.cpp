#include "app/chat_event_bridge.hpp"
#include "core_types/typed_ids.hpp"
#include "presentation/chat_ui.hpp"
#include "tool_call/types.hpp"

#include <optional>
#include <regex>
#include <string>
#include <utility>
#include <variant>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::app;
using namespace yac::chat;
using namespace yac::presentation;
using namespace yac::tool_call;
using yac::SubAgentId;

namespace {

ChatEvent MakeToolStartedEvent(ChatMessageId id, std::string agent_id,
                               std::string task) {
  return ChatEvent{ToolCallStartedEvent{
      .message_id = id,
      .role = ChatRole::Tool,
      .tool_name = std::string(kSubAgentToolName),
      .tool_call = SubAgentCall{.task = task,
                                .status = SubAgentStatus::Running,
                                .agent_id = std::move(agent_id)},
      .status = ChatMessageStatus::Active}};
}

std::string RenderComponent(const ftxui::Component& component, int width = 100,
                            int height = 40) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

std::string StripAnsi(const std::string& value) {
  static const std::regex ansi("\x1b\\[[^A-Za-z]*[A-Za-z]");
  return std::regex_replace(value, ansi, "");
}

std::vector<std::string> Lines(const std::string& value) {
  std::vector<std::string> lines;
  size_t start = 0;
  while (start <= value.size()) {
    auto pos = value.find('\n', start);
    if (pos == std::string::npos) {
      pos = value.size();
    }
    lines.push_back(value.substr(start, pos - start));
    if (pos == value.size()) {
      break;
    }
    start = pos + 1;
  }
  return lines;
}

std::optional<std::pair<int, int>> FindTextPosition(const std::string& rendered,
                                                    const std::string& needle) {
  const auto lines = Lines(StripAnsi(rendered));
  for (size_t y = 0; y < lines.size(); ++y) {
    const auto x = lines[y].find(needle);
    if (x != std::string::npos) {
      return std::pair{static_cast<int>(x), static_cast<int>(y)};
    }
  }
  return std::nullopt;
}

ftxui::Event MakeMouseLeftPress(int x, int y) {
  ftxui::Mouse mouse;
  mouse.button = ftxui::Mouse::Left;
  mouse.motion = ftxui::Mouse::Pressed;
  mouse.x = x;
  mouse.y = y;
  return ftxui::Event::Mouse("", mouse);
}

void EmitSubAgentChildTool(ChatEventBridge& bridge, ChatMessageId id,
                           std::string agent_id) {
  const auto* const task = "inspect workspace";
  bridge.HandleEvent(ChatEvent{SubAgentProgressEvent{
      .message_id = id,
      .sub_agent_id = SubAgentId{std::move(agent_id)},
      .sub_agent_task = task,
      .sub_agent_tool_count = 1,
      .child_tool = SubAgentChildToolEvent{
          .tool_call_id = yac::ToolCallId{"tool-1"},
          .tool_name = std::string(kListDirToolName),
          .tool_call = ListDirCall{.path = "src",
                                   .entries = {{"main.cpp",
                                                DirectoryEntryType::File, 10}},
                                   .truncated = false,
                                   .is_error = false,
                                   .error = ""},
          .status = ChatMessageStatus::Complete}}});
}

void SeedSubAgentWithChildTool(ChatEventBridge& bridge, ChatMessageId id,
                               const std::string& agent_id) {
  const auto* const task = "inspect workspace";
  bridge.HandleEvent(MakeToolStartedEvent(id, agent_id, task));
  EmitSubAgentChildTool(bridge, id, agent_id);
}

}  // namespace

TEST_CASE(
    "Bridge creates a single card via ToolCallStarted with SubAgentCall") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(50, "agent-1", "analyze"));

  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE(ui.GetMessages()[0].sender == Sender::Agent);
  const auto* tool = ui.GetMessages()[0].FindToolSegment(50);
  REQUIRE(tool != nullptr);
  REQUIRE(tool->status == MessageStatus::Active);
  const auto& sub = std::get<SubAgentCall>(tool->block);
  REQUIRE(sub.task == "analyze");
  REQUIRE(sub.status == SubAgentStatus::Running);
  REQUIRE(sub.agent_id == "agent-1");
}

TEST_CASE(
    "Bridge updates the same card on SubAgentCompleted and shows "
    "notification") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(51, "agent-2", "run all tests"));

  bridge.HandleEvent(ChatEvent{SubAgentCompletedEvent{
      .message_id = 51,
      .sub_agent_id = SubAgentId{"agent-2"},
      .sub_agent_task = "run all tests",
      .sub_agent_result = "all 42 tests passed",
      .sub_agent_tool_count = 5,
      .sub_agent_elapsed_ms = 1200,
  }});

  REQUIRE(ui.GetMessages().size() == 1);
  const auto* tool = ui.GetMessages()[0].FindToolSegment(51);
  REQUIRE(tool != nullptr);
  REQUIRE(tool->status == MessageStatus::Complete);
  const auto& sub = std::get<SubAgentCall>(tool->block);
  REQUIRE(sub.status == SubAgentStatus::Complete);
  REQUIRE(sub.result == "all 42 tests passed");
  REQUIRE(sub.tool_count == 5);
  REQUIRE(sub.elapsed_ms == 1200);
}

TEST_CASE("Bridge handles SubAgentError -- updates card with error status") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(52, "agent-3", "fetch remote data"));

  bridge.HandleEvent(ChatEvent{SubAgentErrorEvent{
      .message_id = 52,
      .sub_agent_id = SubAgentId{"agent-3"},
      .sub_agent_task = "fetch remote data",
      .sub_agent_result = "connection refused",
  }});

  REQUIRE(ui.GetMessages().size() == 1);
  const auto* tool = ui.GetMessages()[0].FindToolSegment(52);
  REQUIRE(tool != nullptr);
  REQUIRE(tool->status == MessageStatus::Error);
  const auto& sub = std::get<SubAgentCall>(tool->block);
  REQUIRE(sub.status == SubAgentStatus::Error);
  REQUIRE(sub.result == "connection refused");
}

TEST_CASE(
    "Bridge handles SubAgentCancelled -- updates card with cancelled "
    "status") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(53, "agent-4", "long running task"));

  bridge.HandleEvent(ChatEvent{SubAgentCancelledEvent{
      .message_id = 53,
      .sub_agent_id = SubAgentId{"agent-4"},
      .sub_agent_task = "long running task",
  }});

  REQUIRE(ui.GetMessages().size() == 1);
  const auto* tool = ui.GetMessages()[0].FindToolSegment(53);
  REQUIRE(tool != nullptr);
  REQUIRE(tool->status == MessageStatus::Cancelled);
  const auto& sub = std::get<SubAgentCall>(tool->block);
  REQUIRE(sub.status == SubAgentStatus::Cancelled);
}

TEST_CASE("Bridge handles SubAgentProgress -- updates card with tool count") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(54, "agent-5", "progressive task"));

  bridge.HandleEvent(ChatEvent{SubAgentProgressEvent{
      .message_id = 54,
      .sub_agent_id = SubAgentId{"agent-5"},
      .sub_agent_task = "progressive task",
      .sub_agent_tool_count = 3,
  }});

  REQUIRE(ui.GetMessages().size() == 1);
  const auto* tool = ui.GetMessages()[0].FindToolSegment(54);
  REQUIRE(tool != nullptr);
  REQUIRE(tool->status == MessageStatus::Active);
  const auto& sub = std::get<SubAgentCall>(tool->block);
  REQUIRE(sub.status == SubAgentStatus::Running);
  REQUIRE(sub.tool_count == 3);
}

TEST_CASE("Sub-agent tool calls render inside the sub-agent dropdown") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  SeedSubAgentWithChildTool(bridge, 55, "agent-6");

  auto component = ui.Build();
  auto collapsed = RenderComponent(component);
  REQUIRE(ui.GetMessages().size() == 1);
  REQUIRE_THAT(collapsed,
               !Catch::Matchers::ContainsSubstring("Sub-agent tool calls"));

  ChatUI expanded_ui;
  ChatEventBridge expanded_bridge(expanded_ui);
  expanded_bridge.HandleEvent(
      MakeToolStartedEvent(55, "agent-6", "inspect workspace"));
  expanded_ui.SetToolExpanded(55, true);
  EmitSubAgentChildTool(expanded_bridge, 55, "agent-6");

  auto expanded_component = expanded_ui.Build();
  auto expanded = RenderComponent(expanded_component);
  REQUIRE_THAT(expanded,
               Catch::Matchers::ContainsSubstring("Sub-agent tool calls (1)"));
  REQUIRE_THAT(expanded, Catch::Matchers::ContainsSubstring("List directory"));
  REQUIRE_THAT(expanded, !Catch::Matchers::ContainsSubstring("main.cpp"));
}

TEST_CASE("Completed sub-agent and nested tool boxes remain mouse toggleable") {
  ChatUI ui;
  ChatEventBridge bridge(ui);

  bridge.HandleEvent(MakeToolStartedEvent(56, "agent-7", "inspect workspace"));
  ui.SetToolExpanded(56, true);
  EmitSubAgentChildTool(bridge, 56, "agent-7");

  auto component = ui.Build();
  auto output = RenderComponent(component);
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Sub-agent tool calls (1)"));
  auto nested_tool_position = FindTextPosition(output, "List directory");
  REQUIRE(nested_tool_position.has_value());
  REQUIRE(component->OnEvent(MakeMouseLeftPress(nested_tool_position->first,
                                                nested_tool_position->second)));

  bridge.HandleEvent(ChatEvent{SubAgentCompletedEvent{
      .message_id = 56,
      .sub_agent_id = SubAgentId{"agent-7"},
      .sub_agent_task = "inspect workspace",
      .sub_agent_result = "done",
      .sub_agent_tool_count = 1,
  }});

  output = RenderComponent(component);
  auto sub_agent_position = FindTextPosition(output, "[>] Sub-agent");
  REQUIRE(sub_agent_position.has_value());
  REQUIRE(component->OnEvent(MakeMouseLeftPress(sub_agent_position->first,
                                                sub_agent_position->second)));
}
