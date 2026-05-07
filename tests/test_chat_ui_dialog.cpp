#include "presentation/chat_ui.hpp"
#include "util/mock_chat_actions.hpp"

#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;
using yac::test::MockChatActions;

namespace {

ftxui::Event MakeCtrlP() {
  return ftxui::Event::Special("\x10");
}

std::string RenderComponent(const ftxui::Component& component, int width = 100,
                            int height = 30) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

std::vector<Command> SampleCommands() {
  return {
      {"Open File", "Browse project files"},
      {"Focus Composer", "Jump to input editor"},
  };
}

std::vector<Command> SampleModelCommands() {
  return {
      {"switch_model:glm-5.1", "glm-5.1", "Use glm-5.1 for future responses"},
      {"switch_model:glm-4.7", "glm-4.7", "Use glm-4.7 for future responses"},
  };
}

}  // namespace

TEST_CASE("ChatUI renders help hint chip in status rail") {
  ChatUI ui;
  auto component = ui.Build();
  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("[? help]"));
}

TEST_CASE("ChatUI renders active assistant while streaming") {
  ChatUI ui;
  auto component = ui.Build();

  std::string long_prompt;
  for (int i = 0; i < 600; ++i) {
    long_prompt += "story ";
  }
  ui.AddMessage(Sender::User, long_prompt);

  auto id = ui.StartAgentMessage();

  auto started_output = RenderComponent(component, 80, 24);
  REQUIRE_THAT(started_output, Catch::Matchers::ContainsSubstring("Assistant"));
  REQUIRE_THAT(started_output, Catch::Matchers::ContainsSubstring("thinking"));

  ui.AppendToAgentMessage(id, long_prompt);

  auto streaming_output = RenderComponent(component, 80, 24);
  REQUIRE_THAT(streaming_output, Catch::Matchers::ContainsSubstring("story"));
  REQUIRE_THAT(streaming_output,
               Catch::Matchers::ContainsSubstring("\xe2\x96\x8d"));
}

TEST_CASE("ChatUI wraps long word-spaced messages") {
  ChatUI ui;
  auto component = ui.Build();

  std::string long_text;
  for (int i = 0; i < 250; ++i) {
    long_text += "word ";
  }
  long_text += "TAIL_MARKER";

  SECTION("user message") {
    ui.AddMessage(Sender::User, long_text);
  }

  SECTION("assistant message") {
    ui.AddMessage(Sender::Agent, long_text);
  }

  auto output = RenderComponent(component, 80, 24);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("TAIL_MARKER"));
}

TEST_CASE("ChatUI opens and closes command palette dialog") {
  ChatUI ui;
  ui.SetCommands(SampleCommands());
  auto component = ui.Build();

  auto hidden_output = RenderComponent(component);
  REQUIRE_THAT(hidden_output,
               !Catch::Matchers::ContainsSubstring("Command Palette"));
  REQUIRE_THAT(hidden_output, !Catch::Matchers::ContainsSubstring("Open File"));

  REQUIRE(component->OnEvent(MakeCtrlP()));

  auto shown_output = RenderComponent(component);
  REQUIRE_THAT(shown_output,
               Catch::Matchers::ContainsSubstring("Command Palette"));
  REQUIRE_THAT(shown_output, Catch::Matchers::ContainsSubstring("Open File"));
  REQUIRE_THAT(shown_output,
               Catch::Matchers::ContainsSubstring("Focus Composer"));

  REQUIRE(component->OnEvent(ftxui::Event::Escape));

  auto closed_output = RenderComponent(component);
  REQUIRE_THAT(closed_output,
               !Catch::Matchers::ContainsSubstring("Command Palette"));
  REQUIRE_THAT(closed_output, !Catch::Matchers::ContainsSubstring("Open File"));
}

TEST_CASE("ChatUI opens help from command palette") {
  MockChatActions actions;
  ChatUI ui(actions);
  actions.on_command = [&](const std::string& command) {
    if (command == "help") {
      ui.ShowHelp();
    }
  };
  ui.SetHelpText("Help body with shortcuts and setup.");
  ui.SetCommands({{"help", "Help", "Show shortcuts and setup status"}});
  auto component = ui.Build();

  REQUIRE(component->OnEvent(MakeCtrlP()));
  REQUIRE(component->OnEvent(ftxui::Event::Return));

  auto output = RenderComponent(component);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Help"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Help body with shortcuts"));

  REQUIRE(component->OnEvent(ftxui::Event::Escape));

  auto closed_output = RenderComponent(component);
  REQUIRE_THAT(closed_output,
               !Catch::Matchers::ContainsSubstring("Help body with shortcuts"));
}

TEST_CASE("ChatUI opens model picker from command palette") {
  MockChatActions actions;
  ChatUI ui(actions);
  ui.SetCommands({{"switch_model", "Switch Model",
                   "Choose the model for future responses"}});
  ui.SetModelCommands(SampleModelCommands());
  auto component = ui.Build();

  REQUIRE(component->OnEvent(MakeCtrlP()));
  REQUIRE(component->OnEvent(ftxui::Event::Return));

  auto model_picker_output = RenderComponent(component);
  REQUIRE_THAT(model_picker_output,
               Catch::Matchers::ContainsSubstring("Switch Model"));
  REQUIRE_THAT(model_picker_output,
               Catch::Matchers::ContainsSubstring("glm-5.1"));
  REQUIRE_THAT(model_picker_output,
               Catch::Matchers::ContainsSubstring("glm-4.7"));

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(actions.commands == std::vector<std::string>{"switch_model:glm-5.1"});
}
