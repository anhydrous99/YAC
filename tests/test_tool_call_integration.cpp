#include "presentation/chat_ui.hpp"
#include "presentation/message.hpp"
#include "presentation/message_renderer.hpp"
#include "tool_call/types.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;
using namespace yac::tool_call;

namespace {

std::string RenderElement(const ftxui::Element& element, int width = 100,
                          int height = 30) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, element);
  return screen.ToString();
}

std::string RenderComponent(const ftxui::Component& component, int width = 100,
                            int height = 30) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

size_t CountOccurrences(const std::string& haystack,
                        const std::string& needle) {
  if (needle.empty()) {
    return 0;
  }

  size_t count = 0;
  size_t pos = 0;
  while ((pos = haystack.find(needle, pos)) != std::string::npos) {
    ++count;
    pos += needle.size();
  }
  return count;
}

Message MakeAgentWithTool(ToolCallBlock block,
                          MessageStatus tool_status = MessageStatus::Complete) {
  Message msg{Sender::Agent, ""};
  msg.segments.clear();
  msg.segments.emplace_back(
      ToolSegment{.id = 1, .block = std::move(block), .status = tool_status});
  return msg;
}

}  // namespace

TEST_CASE("MessageRenderer renders agent message containing a bash tool") {
  auto message = MakeAgentWithTool(BashCall{.command = "ls",
                                            .output = "main.cpp",
                                            .exit_code = 0,
                                            .is_error = false});

  auto output = RenderElement(MessageRenderer::Render(message, 80));

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("bash"));
}

TEST_CASE(
    "MessageRenderer RenderAll renders user, agent-with-tool, and agent-text "
    "in order") {
  std::vector<Message> messages;
  messages.emplace_back(Sender::User, "hello from user");
  messages.push_back(MakeAgentWithTool(BashCall{.command = "pwd",
                                                .output = "/tmp/project",
                                                .exit_code = 0,
                                                .is_error = false}));
  messages.emplace_back(Sender::Agent, "agent response");

  auto output = RenderElement(MessageRenderer::RenderAll(messages, 80));

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("hello from user"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("bash"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("agent"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("response"));
}

TEST_CASE("ChatUI tool messages render collapsed chevron by default") {
  ChatUI ui;
  ui.AddToolCallMessage(BashCall{.command = "git status",
                                 .output = "working tree clean",
                                 .exit_code = 0,
                                 .is_error = false});

  auto component = ui.Build();
  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x96\xb6"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Run command"));
}

TEST_CASE("ChatUI SetToolExpanded collapsed hides tool content") {
  ChatUI ui;
  auto tool_id = ui.AddToolCallMessage(BashCall{.command = "git status",
                                                .output = "working tree clean",
                                                .exit_code = 0,
                                                .is_error = false});
  ui.SetToolExpanded(tool_id, false);

  auto component = ui.Build();
  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x96\xb6"));
  REQUIRE_THAT(output,
               !Catch::Matchers::ContainsSubstring("working tree clean"));
}

TEST_CASE("AddToolCallMessage increases tool block count") {
  ChatUI ui;
  ui.AddToolCallMessage(BashCall{.command = "pwd",
                                 .output = "/tmp/one",
                                 .exit_code = 0,
                                 .is_error = false});
  ui.AddToolCallMessage(BashCall{.command = "pwd",
                                 .output = "/tmp/two",
                                 .exit_code = 0,
                                 .is_error = false});

  auto component = ui.Build();
  auto output = RenderComponent(component);

  REQUIRE(CountOccurrences(output, "Run command") >= 2);
}

TEST_CASE("ChatUI renders tool segment after streamed text in emission order") {
  ChatUI ui;
  auto agent_id = ui.StartAgentMessage();
  ui.AppendToAgentMessage(agent_id, "running the command");
  ui.AddToolCallMessage(BashCall{
      .command = "ls", .output = "/tmp", .exit_code = 0, .is_error = false});
  ui.AppendToAgentMessage(agent_id, "all done");

  const auto& msg = ui.GetMessages()[0];
  REQUIRE(msg.segments.size() == 3);
  REQUIRE(std::holds_alternative<TextSegment>(msg.segments[0]));
  REQUIRE(std::holds_alternative<ToolSegment>(msg.segments[1]));
  REQUIRE(std::holds_alternative<TextSegment>(msg.segments[2]));
}
