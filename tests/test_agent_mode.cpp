#include "chat/agent_mode.hpp"
#include "chat/chat_history_store.hpp"
#include "chat/chat_service.hpp"
#include "provider/provider_registry.hpp"

#include <chrono>
#include <ctime>
#include <filesystem>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::chat;

namespace {

class TempWorkspace {
 public:
  TempWorkspace() {
    path_ = std::filesystem::temp_directory_path() /
            ("yac_plan_mode_test_" +
             std::to_string(
                 std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::remove_all(path_);
    std::filesystem::create_directories(path_);
  }

  ~TempWorkspace() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
  }

  TempWorkspace(const TempWorkspace&) = delete;
  TempWorkspace& operator=(const TempWorkspace&) = delete;
  TempWorkspace(TempWorkspace&&) = delete;
  TempWorkspace& operator=(TempWorkspace&&) = delete;

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  std::filesystem::path path_;
};

std::chrono::system_clock::time_point LocalTime(int year, int month, int day,
                                                int hour, int minute,
                                                int second) {
  std::tm tm{};
  tm.tm_year = year - 1900;
  tm.tm_mon = month - 1;
  tm.tm_mday = day;
  tm.tm_hour = hour;
  tm.tm_min = minute;
  tm.tm_sec = second;
  tm.tm_isdst = -1;
  return std::chrono::system_clock::from_time_t(std::mktime(&tm));
}

ChatConfig ConfigForWorkspace(const TempWorkspace& workspace) {
  ChatConfig config;
  config.workspace_root = workspace.Path().string();
  return config;
}

ChatService MakeService(ChatConfig config) {
  yac::provider::ProviderRegistry registry;
  return ChatService(std::move(registry), std::move(config));
}

bool IsContainedBy(const std::filesystem::path& parent,
                   const std::filesystem::path& child) {
  const auto relative =
      std::filesystem::absolute(child).lexically_normal().lexically_relative(
          std::filesystem::absolute(parent).lexically_normal());
  return !relative.empty() && *relative.begin() != "..";
}

}  // namespace

TEST_CASE("Agent mode tool filtering", "[agent_mode]") {
  SECTION("Build mode excludes no statically denied tools") {
    const auto excluded = ExcludedToolsForMode(AgentMode::Build);

    REQUIRE(excluded.empty());
    REQUIRE_FALSE(IsToolAllowedForMode(AgentMode::Build, "plan_exit"));
  }

  SECTION("Plan mode excludes write/mutate tools") {
    const auto excluded = ExcludedToolsForMode(AgentMode::Plan);
    const std::set<std::string> expected{
        "bash",
        "file_edit",
        "file_write",
        "lsp_rename",
    };

    REQUIRE(excluded == expected);
  }

  SECTION("Plan mode allows read-only tools and plan approval tools") {
    for (const std::string tool :
         {"file_read", "list_dir", "glob", "grep", "lsp_diagnostics",
          "lsp_references", "lsp_goto_definition", "lsp_symbols", "sub_agent",
          "ask_user", "todo_write", "plan_exit"}) {
      CAPTURE(tool);
      REQUIRE(IsToolAllowedForMode(AgentMode::Plan, tool));
    }
  }

  SECTION("Plan mode denies side-effect and MCP tools") {
    for (const std::string tool :
         {"bash", "file_write", "file_edit", "lsp_rename", "mcp_fs__read",
          "mcp_search__query"}) {
      CAPTURE(tool);
      REQUIRE_FALSE(IsToolAllowedForMode(AgentMode::Plan, tool));
    }
  }

  SECTION("Plan mode filters denied built-ins and all MCP tools from catalog") {
    std::vector<ToolDefinition> tools{
        {.name = "file_read"},  {.name = "file_write"},  {.name = "file_edit"},
        {.name = "todo_write"}, {.name = "plan_exit"},   {.name = "bash"},
        {.name = "lsp_rename"}, {.name = "mcp_fs__read"}};

    ChatHistoryStore::FilterToolsForAgentMode(
        tools, {}, ExcludedToolsForMode(AgentMode::Plan), AgentMode::Plan);

    std::set<std::string> remaining;
    for (const auto& tool : tools) {
      remaining.insert(tool.name);
    }
    REQUIRE(remaining ==
            std::set<std::string>{"file_read", "plan_exit", "todo_write"});
  }
}

TEST_CASE("Plan mode session path state", "[agent_mode]") {
  SECTION("defaults to Build mode without plan state") {
    TempWorkspace workspace;
    auto service = MakeService(ConfigForWorkspace(workspace));

    REQUIRE(service.GetAgentMode() == AgentMode::Build);
    REQUIRE_FALSE(service.HasEnteredPlanMode());
    REQUIRE_FALSE(service.ActivePlanPath().has_value());
    REQUIRE_FALSE(std::filesystem::exists(workspace.Path() / ".opencode"));
  }

  SECTION("Build mode submit does not create plan artifacts") {
    TempWorkspace workspace;
    auto service = MakeService(ConfigForWorkspace(workspace));

    service.SubmitUserMessage("build this without plan mode");

    REQUIRE_FALSE(service.HasEnteredPlanMode());
    REQUIRE_FALSE(service.ActivePlanPath().has_value());
    REQUIRE_FALSE(std::filesystem::exists(workspace.Path() / ".opencode"));
  }

  SECTION("first Plan prompt creates deterministic workspace plan file") {
    TempWorkspace workspace;
    auto service = MakeService(ConfigForWorkspace(workspace));
    service.SetPlanClockForTest(
        [] { return LocalTime(2026, 5, 22, 14, 30, 45); });

    service.SetAgentMode(AgentMode::Plan);
    service.SubmitUserMessage(
        "Build a PLAN: with Symbols + Spaces / and a Very Long Tail");

    const auto active_path = service.ActivePlanPath();
    REQUIRE(service.HasEnteredPlanMode());
    REQUIRE(active_path.has_value());
    REQUIRE(*active_path == workspace.Path() / ".opencode" / "plans" /
                                "20260522-143045-build-a-plan-with-symbols-"
                                "spaces-and-a-v.md");
    REQUIRE(std::filesystem::exists(*active_path));
    REQUIRE(
        IsContainedBy(workspace.Path() / ".opencode" / "plans", *active_path));
  }

  SECTION("plan prompt cannot escape the workspace through slug content") {
    TempWorkspace workspace;
    auto service = MakeService(ConfigForWorkspace(workspace));
    service.SetPlanClockForTest([] { return LocalTime(2026, 1, 2, 3, 4, 5); });

    service.SetAgentMode(AgentMode::Plan);
    service.SubmitUserMessage("../escape/../../outside");

    const auto active_path = service.ActivePlanPath();
    REQUIRE(active_path.has_value());
    REQUIRE(*active_path == workspace.Path() / ".opencode" / "plans" /
                                "20260102-030405-escape-outside.md");
    REQUIRE(IsContainedBy(workspace.Path(), *active_path));
    REQUIRE(std::filesystem::exists(*active_path));
    REQUIRE_FALSE(
        std::filesystem::exists(workspace.Path().parent_path() / "outside.md"));
  }

  SECTION("reset clears Plan mode and plan session state") {
    TempWorkspace workspace;
    auto service = MakeService(ConfigForWorkspace(workspace));
    service.SetPlanClockForTest(
        [] { return LocalTime(2026, 5, 22, 14, 30, 45); });
    service.SetAgentMode(AgentMode::Plan);
    service.SubmitUserMessage("make a plan");
    REQUIRE(service.ActivePlanPath().has_value());

    service.ResetConversation();

    REQUIRE(service.GetAgentMode() == AgentMode::Build);
    REQUIRE_FALSE(service.HasEnteredPlanMode());
    REQUIRE_FALSE(service.ActivePlanPath().has_value());
  }
}
