#include "presentation/composer_state.hpp"
#include "presentation/slash_command_registry.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using yac::presentation::ComposerState;
using yac::presentation::SlashCommand;
using yac::presentation::SlashCommandRegistry;

namespace {
auto MakeCommand(const std::string& name, const std::string& desc,
                 std::vector<std::string> aliases = {})
    -> SlashCommand {
  return {.id = name,
          .name = name,
          .description = desc,
          .aliases = std::move(aliases),
          .handler = std::nullopt};
}
}  // namespace

TEST_CASE("ComposerState slash menu is inactive by default",
          "[composer_state]") {
  ComposerState state;
  CHECK_FALSE(state.IsSlashMenuActive());
}

TEST_CASE("ComposerState ActivateSlashMenu activates and resets selection",
          "[composer_state]") {
  ComposerState state;
  state.ActivateSlashMenu();
  CHECK(state.IsSlashMenuActive());
  CHECK(state.SlashMenuSelectedIndex() == 0);
}

TEST_CASE("ComposerState DismissSlashMenu deactivates", "[composer_state]") {
  ComposerState state;
  state.ActivateSlashMenu();
  state.DismissSlashMenu();
  CHECK_FALSE(state.IsSlashMenuActive());
}

TEST_CASE("ComposerState SlashMenuFilter returns empty for non-slash content",
          "[composer_state]") {
  ComposerState state;
  state.Content() = "hello";
  *state.CursorPosition() = 5;
  CHECK(state.SlashMenuFilter().empty());
}

TEST_CASE("ComposerState SlashMenuFilter returns text after slash",
          "[composer_state]") {
  ComposerState state;
  state.Content() = "/qu";
  *state.CursorPosition() = 3;
  CHECK(state.SlashMenuFilter() == "qu");
}

TEST_CASE("ComposerState SlashMenuFilter returns empty for bare slash",
          "[composer_state]") {
  ComposerState state;
  state.Content() = "/";
  *state.CursorPosition() = 1;
  CHECK(state.SlashMenuFilter().empty());
}

TEST_CASE("ComposerState SlashMenuFilter returns empty when cursor at start",
          "[composer_state]") {
  ComposerState state;
  state.Content() = "/quit";
  *state.CursorPosition() = 0;
  CHECK(state.SlashMenuFilter().empty());
}

TEST_CASE("ComposerState FilteredSlashIndices returns all for empty filter",
          "[composer_state]") {
  ComposerState state;
  state.Content() = "/";
  *state.CursorPosition() = 1;
  std::vector<SlashCommand> commands = {MakeCommand("quit", "Exit"),
                                        MakeCommand("clear", "Clear")};
  auto indices = state.FilteredSlashIndices(commands);
  REQUIRE(indices.size() == 2);
  CHECK(indices[0] == 0);
  CHECK(indices[1] == 1);
}

TEST_CASE("ComposerState FilteredSlashIndices filters by prefix",
          "[composer_state]") {
  ComposerState state;
  state.Content() = "/qu";
  *state.CursorPosition() = 3;
  std::vector<SlashCommand> commands = {MakeCommand("quit", "Exit"),
                                        MakeCommand("clear", "Clear")};
  auto indices = state.FilteredSlashIndices(commands);
  REQUIRE(indices.size() == 1);
  CHECK(indices[0] == 0);
}

TEST_CASE("ComposerState FilteredSlashIndices returns empty for no match",
          "[composer_state]") {
  ComposerState state;
  state.Content() = "/xyz";
  *state.CursorPosition() = 4;
  std::vector<SlashCommand> commands = {MakeCommand("quit", "Exit"),
                                        MakeCommand("clear", "Clear")};
  auto indices = state.FilteredSlashIndices(commands);
  CHECK(indices.empty());
}

TEST_CASE("ComposerState FilteredSlashIndices matches by alias",
          "[composer_state]") {
  ComposerState state;
  state.Content() = "/ex";
  *state.CursorPosition() = 3;
  std::vector<SlashCommand> commands = {
      MakeCommand("quit", "Exit", {"exit"})};
  auto indices = state.FilteredSlashIndices(commands);
  REQUIRE(indices.size() == 1);
  CHECK(indices[0] == 0);
}

TEST_CASE("ComposerState Submit resets slash menu state", "[composer_state]") {
  ComposerState state;
  state.Content() = "/quit";
  *state.CursorPosition() = 5;
  state.ActivateSlashMenu();
  state.SetSlashMenuSelectedIndex(1);

  auto submitted = state.Submit();
  CHECK(submitted == "/quit");
  CHECK_FALSE(state.IsSlashMenuActive());
  CHECK(state.SlashMenuSelectedIndex() == 0);
}

TEST_CASE("ComposerState SetSlashMenuSelectedIndex updates index",
          "[composer_state]") {
  ComposerState state;
  state.SetSlashMenuSelectedIndex(3);
  CHECK(state.SlashMenuSelectedIndex() == 3);
}
