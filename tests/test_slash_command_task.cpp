#include "presentation/slash_command_registry.hpp"

#include <string>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using yac::presentation::SlashCommandRegistry;

TEST_CASE(
    "SlashCommandRegistry arguments_handler receives text after command name",
    "[slash_command][task]") {
  SlashCommandRegistry registry;
  registry.Define("task", "task", "Test task command");

  std::string received_args;
  registry.SetArgumentsHandler("task", [&received_args](std::string args) {
    received_args = std::move(args);
  });

  REQUIRE(registry.TryDispatch("/task hello world"));
  REQUIRE(received_args == "hello world");
}

TEST_CASE("SlashCommandRegistry arguments_handler trims leading whitespace",
          "[slash_command][task]") {
  SlashCommandRegistry registry;
  registry.Define("task", "task", "Test task command");

  std::string received_args;
  registry.SetArgumentsHandler("task", [&received_args](std::string args) {
    received_args = std::move(args);
  });

  REQUIRE(registry.TryDispatch("/task   hello"));
  REQUIRE(received_args == "hello");
}

TEST_CASE("SlashCommandRegistry empty task dispatches with empty string",
          "[slash_command][task]") {
  SlashCommandRegistry registry;
  registry.Define("task", "task", "Test task command");

  std::string received_args;
  registry.SetArgumentsHandler("task", [&received_args](std::string args) {
    received_args = std::move(args);
  });

  REQUIRE(registry.TryDispatch("/task"));
  REQUIRE(received_args.empty());
}

TEST_CASE(
    "SlashCommandRegistry existing commands without arguments_handler still "
    "dispatch normally",
    "[slash_command][task]") {
  SlashCommandRegistry registry;
  registry.Define("help", "help", "Show help");

  bool handler_called = false;
  registry.SetHandler("help", [&handler_called] { handler_called = true; });

  REQUIRE(registry.TryDispatch("/help"));
  REQUIRE(handler_called);
}

TEST_CASE(
    "SlashCommandRegistry arguments_handler takes precedence over handler when "
    "both set",
    "[slash_command][task]") {
  SlashCommandRegistry registry;
  registry.Define("task", "task", "Test task command");

  bool handler_called = false;
  bool args_handler_called = false;
  registry.SetHandler("task", [&handler_called] { handler_called = true; });
  registry.SetArgumentsHandler("task", [&args_handler_called](std::string) {
    args_handler_called = true;
  });

  REQUIRE(registry.TryDispatch("/task something"));
  REQUIRE(args_handler_called);
  REQUIRE_FALSE(handler_called);
}

TEST_CASE("SlashCommandRegistry unknown command returns false",
          "[slash_command][task]") {
  SlashCommandRegistry registry;
  registry.Define("task", "task", "Test task command");

  REQUIRE_FALSE(registry.TryDispatch("/unknown"));
}
