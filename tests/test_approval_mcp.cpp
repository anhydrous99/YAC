#include "core_types/typed_ids.hpp"
#include "presentation/chat_ui.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;
using namespace yac::tool_call;
using yac::ApprovalId;

namespace {

std::string RenderComponent(const ftxui::Component& component, int width = 100,
                            int height = 30) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

std::string StripAnsi(const std::string& in) {
  std::string out;
  out.reserve(in.size());
  for (size_t i = 0; i < in.size(); ++i) {
    if (in[i] == '\x1b' && i + 1 < in.size() && in[i + 1] == '[') {
      i += 2;
      while (i < in.size() && in[i] != 'm') {
        ++i;
      }
      continue;
    }
    out.push_back(in[i]);
  }
  return out;
}

}  // namespace

TEST_CASE("mcp_banner_and_cap") {
  ChatUI ui;
  auto component = ui.Build();

  McpToolCall mcp_call{
      .server_id = "github",
      .tool_name = "mcp_github__create_issue",
      .original_tool_name = "create_issue",
      .arguments_json =
          R"({"title":"Fix memory leak in parser","body":"The parser leaks memory when processing large inputs","labels":["bug","priority:high"]})",
      .server_requires_approval = true,
      .approval_required_tools = {},
  };

  ui.ShowToolApproval(ApprovalId{"approval-1"}, "mcp_github__create_issue",
                      "Call MCP tool github/create_issue", mcp_call);

  auto raw = RenderComponent(component, 80, 30);
  auto output = StripAnsi(raw);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("MCP: github"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("title"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Fix memory leak"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring(
                           "Approval required (per-server)"));

  SECTION("large args produce truncation marker") {
    std::string big_arg(3000, 'X');
    auto big_json = R"({"data":")" + big_arg + R"("})";
    McpToolCall big_call{
        .server_id = "large",
        .tool_name = "mcp_large__bulk",
        .original_tool_name = "bulk",
        .arguments_json = big_json,
        .server_requires_approval = true,
    };

    ChatUI big_ui;
    auto big_component = big_ui.Build();
    big_ui.ShowToolApproval(ApprovalId{"approval-2"}, "mcp_large__bulk",
                            "Call MCP tool large/bulk", big_call);

    auto big_output = StripAnsi(RenderComponent(big_component, 80, 30));

    REQUIRE_THAT(big_output,
                 Catch::Matchers::ContainsSubstring("bytes truncated"));
    REQUIRE_THAT(big_output, Catch::Matchers::ContainsSubstring("MCP: large"));
  }
}

TEST_CASE("per_tool_override") {
  ChatUI ui;
  auto component = ui.Build();

  McpToolCall mcp_call{
      .server_id = "github",
      .tool_name = "mcp_github__delete_repo",
      .original_tool_name = "delete_repo",
      .arguments_json = R"({"repo":"example/yac"})",
      .server_requires_approval = false,
      .approval_required_tools = {"delete_repo", "create_repo"},
  };

  ui.ShowToolApproval(ApprovalId{"approval-3"}, "mcp_github__delete_repo",
                      "Call MCP tool github/delete_repo", mcp_call);

  auto raw = RenderComponent(component, 80, 30);
  auto output = StripAnsi(raw);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("MCP: github"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("per-tool"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("delete_repo"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("per-server"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("Default-allow"));
}
