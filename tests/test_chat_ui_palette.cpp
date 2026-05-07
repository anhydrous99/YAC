#include "presentation/chat_ui.hpp"
#include "util/mock_chat_actions.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/event.hpp>

using namespace yac::presentation;
using yac::test::MockChatActions;

namespace {

void TypeText(const ftxui::Component& component, const std::string& text) {
  for (char ch : text) {
    REQUIRE(component->OnEvent(ftxui::Event::Character(ch)));
  }
}

}  // namespace

TEST_CASE("Slash clear command dispatches without sending a message") {
  MockChatActions actions;
  int clear_count = 0;
  ChatUI ui(actions);
  SlashCommandRegistry registry;
  RegisterBuiltinSlashCommands(registry);
  registry.SetHandler("clear", [&] { ++clear_count; });
  ui.SetSlashCommands(std::move(registry));
  auto component = ui.Build();

  TypeText(component, "/clear");
  REQUIRE(component->OnEvent(ftxui::Event::Return));

  REQUIRE(clear_count == 1);
  REQUIRE(actions.sent_messages.empty());

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(clear_count == 1);
  REQUIRE(actions.sent_messages.empty());
}

TEST_CASE("Slash menu Return dispatches argument command without arguments") {
  MockChatActions actions;
  bool called = false;
  std::string captured = "not called";
  ChatUI ui(actions);
  SlashCommandRegistry registry;
  registry.Define("init", "init", "Initialize repository");
  registry.SetArgumentsHandler("init", [&](std::string args) {
    called = true;
    captured = std::move(args);
  });
  ui.SetSlashCommands(std::move(registry));
  auto component = ui.Build();

  TypeText(component, "/init");
  REQUIRE(component->OnEvent(ftxui::Event::Return));

  REQUIRE(called);
  REQUIRE(captured.empty());
  REQUIRE(actions.sent_messages.empty());
}

TEST_CASE("Slash menu Return dismisses unmatched command before raw submit") {
  MockChatActions actions;
  ChatUI ui(actions);
  SlashCommandRegistry registry;
  RegisterBuiltinSlashCommands(registry);
  ui.SetSlashCommands(std::move(registry));
  auto component = ui.Build();

  TypeText(component, "/does-not-exist");

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(actions.sent_messages.empty());

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(actions.sent_messages == std::vector<std::string>{"/does-not-exist"});
}

TEST_CASE("Slash menu Escape preserves raw slash input for later submit") {
  MockChatActions actions;
  ChatUI ui(actions);
  SlashCommandRegistry registry;
  RegisterBuiltinSlashCommands(registry);
  ui.SetSlashCommands(std::move(registry));
  auto component = ui.Build();

  TypeText(component, "/quit-later");

  REQUIRE(component->OnEvent(ftxui::Event::Escape));
  REQUIRE(actions.sent_messages.empty());

  REQUIRE(component->OnEvent(ftxui::Event::Return));
  REQUIRE(actions.sent_messages == std::vector<std::string>{"/quit-later"});
}

TEST_CASE("Slash menu Tab navigation does not toggle mode") {
  MockChatActions actions;
  ChatUI ui(actions);
  SlashCommandRegistry registry;
  RegisterBuiltinSlashCommands(registry);
  ui.SetSlashCommands(std::move(registry));
  auto component = ui.Build();

  TypeText(component, "/");

  REQUIRE(component->OnEvent(ftxui::Event::Tab));
  REQUIRE(actions.mode_toggles == 0);
}
