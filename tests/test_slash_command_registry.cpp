#include "presentation/slash_command_registry.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

using yac::presentation::SlashCommandRegistry;

TEST_CASE("SlashCommandRegistry rejects non-slash input", "[slash_command]") {
  SlashCommandRegistry registry;
  bool called = false;
  registry.Register({"test", "desc", [&] { called = true; }});

  CHECK_FALSE(registry.TryDispatch("hello"));
  CHECK_FALSE(called);
}

TEST_CASE("SlashCommandRegistry rejects empty input", "[slash_command]") {
  SlashCommandRegistry registry;
  bool called = false;
  registry.Register({"test", "desc", [&] { called = true; }});

  CHECK_FALSE(registry.TryDispatch(""));
  CHECK_FALSE(called);
}

TEST_CASE("SlashCommandRegistry dispatches matching command",
          "[slash_command]") {
  SlashCommandRegistry registry;
  bool quit_called = false;
  registry.Register({"quit", "Exit", [&] { quit_called = true; }});

  CHECK(registry.TryDispatch("/quit"));
  CHECK(quit_called);
}

TEST_CASE("SlashCommandRegistry ignores trailing arguments",
          "[slash_command]") {
  SlashCommandRegistry registry;
  bool quit_called = false;
  registry.Register({"quit", "Exit", [&] { quit_called = true; }});

  CHECK(registry.TryDispatch("/quit now"));
  CHECK(quit_called);
}

TEST_CASE("SlashCommandRegistry returns false for unknown command",
          "[slash_command]") {
  SlashCommandRegistry registry;
  registry.Register({"quit", "Exit", [] {}});

  CHECK_FALSE(registry.TryDispatch("/unknown"));
}

TEST_CASE("SlashCommandRegistry dispatches among multiple commands",
          "[slash_command]") {
  SlashCommandRegistry registry;
  bool quit_called = false;
  bool clear_called = false;
  registry.Register({"quit", "Exit", [&] { quit_called = true; }});
  registry.Register({"clear", "Clear", [&] { clear_called = true; }});

  CHECK(registry.TryDispatch("/clear"));
  CHECK_FALSE(quit_called);
  CHECK(clear_called);
}

TEST_CASE("SlashCommandRegistry rejects bare slash", "[slash_command]") {
  SlashCommandRegistry registry;
  registry.Register({"quit", "Exit", [] {}});

  CHECK_FALSE(registry.TryDispatch("/"));
}

TEST_CASE("SlashCommandRegistry Commands returns registered list",
          "[slash_command]") {
  SlashCommandRegistry registry;
  registry.Register({"quit", "Exit", [] {}});
  registry.Register({"clear", "Clear", [] {}});

  const auto& cmds = registry.Commands();
  REQUIRE(cmds.size() == 2);
  CHECK(cmds[0].name == "quit");
  CHECK(cmds[1].name == "clear");
}
