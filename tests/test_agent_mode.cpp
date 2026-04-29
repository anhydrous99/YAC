#include "chat/agent_mode.hpp"

#include <set>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;

TEST_CASE("Agent mode tool filtering", "[agent_mode]") {
  SECTION("Build mode excludes no tools") {
    const auto excluded = ExcludedToolsForMode(AgentMode::Build);

    REQUIRE(excluded.empty());
  }

  SECTION("Plan mode excludes write/mutate tools") {
    const auto excluded = ExcludedToolsForMode(AgentMode::Plan);
    const std::set<std::string> expected{
        "bash",
        "file_write",
        "file_edit",
        "lsp_rename",
    };

    REQUIRE(excluded == expected);
  }
}
