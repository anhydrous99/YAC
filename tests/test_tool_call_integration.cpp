#include "presentation/chat_ui.hpp"
#include "presentation/message.hpp"
#include "presentation/message_renderer.hpp"
#include "presentation/tool_call/types.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;
using namespace yac::presentation::tool_call;

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

}  // namespace

TEST_CASE("MessageRenderer renders tool call message with bash label") {
  auto message = Message::Tool(BashCall{"ls", "main.cpp", 0, false});

  auto output = RenderElement(MessageRenderer::Render(message, 80));

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("bash"));
}

TEST_CASE(
    "MessageRenderer RenderAll includes tool messages alongside other "
    "messages") {
  auto tool_message = Message::Tool(BashCall{"pwd", "/tmp/project", 0, false});

  std::vector<Message> messages = {
      {Sender::User, "hello from user"},
      tool_message,
      {Sender::Agent, "agent response"},
  };

  auto output = RenderElement(MessageRenderer::RenderAll(messages, 80));

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("hello from user"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("bash"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("agent response"));
}

TEST_CASE("ChatUI tool messages render expanded chevron by default") {
  ChatUI ui;
  ui.AddToolCallMessage(BashCall{"git status", "working tree clean", 0, false});

  auto component = ui.Build();
  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x96\xbc"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("git status"));
}

TEST_CASE("ChatUI SetToolExpanded collapsed hides tool content") {
  ChatUI ui;
  ui.AddToolCallMessage(BashCall{"git status", "working tree clean", 0, false});
  ui.SetToolExpanded(0, false);

  auto component = ui.Build();
  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x96\xb6"));
  REQUIRE_THAT(output,
               !Catch::Matchers::ContainsSubstring("working tree clean"));
}

TEST_CASE("AddToolCallMessage increases tool block count") {
  ChatUI ui;
  ui.AddToolCallMessage(BashCall{"pwd", "/tmp/one", 0, false});
  ui.AddToolCallMessage(BashCall{"pwd", "/tmp/two", 0, false});

  auto component = ui.Build();
  auto output = RenderComponent(component);

  REQUIRE(CountOccurrences(output, "bash") >= 2);
}
