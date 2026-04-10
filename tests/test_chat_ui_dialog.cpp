#include "presentation/chat_ui.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;

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

}  // namespace

TEST_CASE("ChatUI renders command shortcut hint in footer") {
  ChatUI ui;
  auto component = ui.Build();
  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Ctrl+P=Commands"));
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
