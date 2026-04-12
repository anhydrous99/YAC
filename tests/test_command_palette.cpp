#include "presentation/command_palette.hpp"

#include <optional>
#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;

namespace {

std::vector<Command> SampleCommands() {
  return {
      {"Open File", "Browse project files"},
      {"Focus Composer", "Jump to input editor"},
      {"Help", "Show available COMMANDS"},
  };
}

std::string RenderComponent(const ftxui::Component& component, int width = 80,
                            int height = 24) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

void TypeText(const ftxui::Component& component, const std::string& text) {
  for (char ch : text) {
    REQUIRE(component->OnEvent(ftxui::Event::Character(ch)));
  }
}

}  // namespace

TEST_CASE("CommandPalette renders full list when filter is empty") {
  bool show = true;
  auto component = CommandPalette(SampleCommands(), [](int) {}, &show);
  auto output = RenderComponent(component, 80, 24);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Type to filter..."));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Open File"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Browse project files"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Focus Composer"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Jump to input editor"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Help"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Show available COMMANDS"));
}

TEST_CASE(
    "CommandPalette filters commands with case-insensitive substring "
    "matching") {
  bool show = true;
  auto component = CommandPalette(SampleCommands(), [](int) {}, &show);

  TypeText(component, "commands");

  auto output = RenderComponent(component, 80, 24);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Help"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Show available COMMANDS"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("Open File"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("Focus Composer"));
}

TEST_CASE("CommandPalette renders empty state when filter has no matches") {
  bool show = true;
  auto component = CommandPalette(SampleCommands(), [](int) {}, &show);

  TypeText(component, "zzz");

  auto output = RenderComponent(component, 80, 24);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("No commands found"));
}

TEST_CASE("CommandPalette arrow navigation wraps across the list") {
  SECTION("Up from the first command selects the last command") {
    bool show = true;
    std::optional<int> selected_index;
    auto component = CommandPalette(
        SampleCommands(), [&](int index) { selected_index = index; }, &show);

    REQUIRE(component->OnEvent(ftxui::Event::ArrowUp));
    REQUIRE(component->OnEvent(ftxui::Event::Return));

    REQUIRE(selected_index == 2);
    REQUIRE_FALSE(show);
  }

  SECTION("Down from the last command wraps back to the first command") {
    bool show = true;
    std::optional<int> selected_index;
    auto component = CommandPalette(
        SampleCommands(), [&](int index) { selected_index = index; }, &show);

    REQUIRE(component->OnEvent(ftxui::Event::ArrowDown));
    REQUIRE(component->OnEvent(ftxui::Event::ArrowDown));
    REQUIRE(component->OnEvent(ftxui::Event::ArrowDown));
    REQUIRE(component->OnEvent(ftxui::Event::Return));

    REQUIRE(selected_index == 0);
    REQUIRE_FALSE(show);
  }
}

TEST_CASE(
    "CommandPalette Enter selects filtered command with original index and "
    "closes") {
  bool show = true;
  std::optional<int> selected_index;
  auto component = CommandPalette(
      SampleCommands(), [&](int index) { selected_index = index; }, &show);

  TypeText(component, "commands");

  REQUIRE(component->OnEvent(ftxui::Event::Return));

  REQUIRE(selected_index == 2);
  REQUIRE_FALSE(show);
}

TEST_CASE("CommandPalette Escape closes without selection") {
  bool show = true;
  bool called = false;
  auto component =
      CommandPalette(SampleCommands(), [&](int) { called = true; }, &show);

  REQUIRE(component->OnEvent(ftxui::Event::Escape));

  REQUIRE_FALSE(show);
  REQUIRE_FALSE(called);
}

TEST_CASE("CommandPalette renders each entry name and description") {
  bool show = true;
  std::vector<Command> commands{{"Status", "Show repository status"}};
  auto component = CommandPalette(commands, [](int) {}, &show);
  auto output = RenderComponent(component, 80, 24);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Status"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Show repository status"));
}

TEST_CASE("Command stores stable id separately from display name") {
  Command command{"new_chat", "New Chat", "Start a fresh conversation"};

  REQUIRE(command.id == "new_chat");
  REQUIRE(command.name == "New Chat");
  REQUIRE(command.description == "Start a fresh conversation");
}
