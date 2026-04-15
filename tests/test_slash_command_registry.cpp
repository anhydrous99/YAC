#include "presentation/slash_command_registry.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>

using yac::presentation::SlashCommand;
using yac::presentation::SlashCommandRegistry;

namespace {
auto MakeCommand(const std::string& name, const std::string& desc,
                 std::function<void()> handler) -> SlashCommand {
  return {.id = name,
          .name = name,
          .description = desc,
          .aliases = {},
          .handler = std::move(handler)};
}
}  // namespace

TEST_CASE("SlashCommandRegistry rejects non-slash input", "[slash_command]") {
  SlashCommandRegistry registry;
  bool called = false;
  registry.Register(MakeCommand("test", "desc", [&] { called = true; }));

  CHECK_FALSE(registry.TryDispatch("hello"));
  CHECK_FALSE(called);
}

TEST_CASE("SlashCommandRegistry rejects empty input", "[slash_command]") {
  SlashCommandRegistry registry;
  bool called = false;
  registry.Register(MakeCommand("test", "desc", [&] { called = true; }));

  CHECK_FALSE(registry.TryDispatch(""));
  CHECK_FALSE(called);
}

TEST_CASE("SlashCommandRegistry dispatches matching command",
          "[slash_command]") {
  SlashCommandRegistry registry;
  bool quit_called = false;
  registry.Register(MakeCommand("quit", "Exit", [&] { quit_called = true; }));

  CHECK(registry.TryDispatch("/quit"));
  CHECK(quit_called);
}

TEST_CASE("SlashCommandRegistry ignores trailing arguments",
          "[slash_command]") {
  SlashCommandRegistry registry;
  bool quit_called = false;
  registry.Register(MakeCommand("quit", "Exit", [&] { quit_called = true; }));

  CHECK(registry.TryDispatch("/quit now"));
  CHECK(quit_called);
}

TEST_CASE("SlashCommandRegistry returns false for unknown command",
          "[slash_command]") {
  SlashCommandRegistry registry;
  registry.Register(MakeCommand("quit", "Exit", [] {}));

  CHECK_FALSE(registry.TryDispatch("/unknown"));
}

TEST_CASE("SlashCommandRegistry dispatches among multiple commands",
          "[slash_command]") {
  SlashCommandRegistry registry;
  bool quit_called = false;
  bool clear_called = false;
  registry.Register(MakeCommand("quit", "Exit", [&] { quit_called = true; }));
  registry.Register(
      MakeCommand("clear", "Clear", [&] { clear_called = true; }));

  CHECK(registry.TryDispatch("/clear"));
  CHECK_FALSE(quit_called);
  CHECK(clear_called);
}

TEST_CASE("SlashCommandRegistry rejects bare slash", "[slash_command]") {
  SlashCommandRegistry registry;
  registry.Register(MakeCommand("quit", "Exit", [] {}));

  CHECK_FALSE(registry.TryDispatch("/"));
}

TEST_CASE("SlashCommandRegistry Commands returns registered list",
          "[slash_command]") {
  SlashCommandRegistry registry;
  registry.Register(MakeCommand("quit", "Exit", [] {}));
  registry.Register(MakeCommand("clear", "Clear", [] {}));

  const auto& cmds = registry.Commands();
  REQUIRE(cmds.size() == 2);
  CHECK(cmds[0].name == "quit");
  CHECK(cmds[1].name == "clear");
}

TEST_CASE("SlashCommandRegistry dispatches via alias", "[slash_command]") {
  SlashCommandRegistry registry;
  bool called = false;
  registry.Define("quit", "quit", "Exit", {"exit"});
  registry.SetHandler("quit", [&] { called = true; });

  CHECK(registry.TryDispatch("/exit"));
  CHECK(called);
}

TEST_CASE("SlashCommandRegistry dispatches via primary name with alias",
          "[slash_command]") {
  SlashCommandRegistry registry;
  bool called = false;
  registry.Define("quit", "quit", "Exit", {"exit"});
  registry.SetHandler("quit", [&] { called = true; });

  CHECK(registry.TryDispatch("/quit"));
  CHECK(called);
}

TEST_CASE("SlashCommandRegistry Define without SetHandler does not dispatch",
          "[slash_command]") {
  SlashCommandRegistry registry;
  registry.Define("quit", "quit", "Exit", {"exit"});

  CHECK_FALSE(registry.TryDispatch("/quit"));
}

TEST_CASE("SlashCommandRegistry SetHandler on unknown id is a no-op",
          "[slash_command]") {
  SlashCommandRegistry registry;
  registry.SetHandler("nonexistent", [] {});
  CHECK(registry.Commands().empty());
}

TEST_CASE("RegisterBuiltinSlashCommands defines quit and clear",
          "[slash_command]") {
  SlashCommandRegistry registry;
  yac::presentation::RegisterBuiltinSlashCommands(registry);

  const auto& cmds = registry.Commands();
  REQUIRE(cmds.size() == 2);
  CHECK(cmds[0].name == "quit");
  REQUIRE(cmds[0].aliases.size() == 1);
  CHECK(cmds[0].aliases[0] == "exit");
  CHECK(cmds[1].name == "clear");
  CHECK(cmds[1].description == "Clear the conversation");
  CHECK(cmds[1].aliases.empty());
}

TEST_CASE("RegisterBuiltinSlashCommands dispatches clear after handler set",
          "[slash_command]") {
  SlashCommandRegistry registry;
  bool called = false;
  yac::presentation::RegisterBuiltinSlashCommands(registry);
  registry.SetHandler("clear", [&] { called = true; });

  CHECK(registry.TryDispatch("/clear"));
  CHECK(called);
}
