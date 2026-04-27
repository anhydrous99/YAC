#include "presentation/mcp/mcp_status_panel.hpp"
#include "presentation/theme.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;

namespace {

std::string RenderComponent(const ftxui::Component& component, int width = 60,
                            int height = 15) {
  auto screen = ftxui::Screen::Create(ftxui::Dimension::Fixed(width),
                                      ftxui::Dimension::Fixed(height));
  ftxui::Render(screen, component->Render());
  return screen.ToString();
}

ftxui::Event MakeMouseLeftPress(int x, int y) {
  ftxui::Mouse mouse;
  mouse.button = ftxui::Mouse::Left;
  mouse.motion = ftxui::Mouse::Pressed;
  mouse.x = x;
  mouse.y = y;
  return ftxui::Event::Mouse("", mouse);
}

}  // namespace

TEST_CASE("renders_counts") {
  theme::InitializeTheme(theme::VividPreset());
  McpStatusSink sink;
  sink.UpdateServer("srv-a", "ready", "");
  sink.UpdateServer("srv-b", "ready", "");
  sink.UpdateServer("srv-c", "failed", "connection refused");

  auto panel = McpStatusPanelComponent(sink);
  auto collapsed_output = RenderComponent(panel);

  REQUIRE_THAT(collapsed_output,
               Catch::Matchers::ContainsSubstring("MCP (2 active"));
  REQUIRE_THAT(collapsed_output, Catch::Matchers::ContainsSubstring("1 error"));

  panel->OnEvent(MakeMouseLeftPress(0, 0));
  auto expanded_output = RenderComponent(panel);
  REQUIRE_THAT(expanded_output,
               Catch::Matchers::ContainsSubstring("\xe2\x9c\x97"));
}

TEST_CASE("toggle_collapse") {
  theme::InitializeTheme(theme::VividPreset());
  McpStatusSink sink;
  sink.UpdateServer("srv-a", "ready", "");
  sink.UpdateServer("srv-b", "ready", "");
  sink.UpdateServer("srv-c", "failed", "timeout");

  auto panel = McpStatusPanelComponent(sink);

  auto before = RenderComponent(panel);

  REQUIRE(panel->OnEvent(MakeMouseLeftPress(0, 0)));

  auto after = RenderComponent(panel);

  REQUIRE(before != after);
  REQUIRE_THAT(after, Catch::Matchers::ContainsSubstring("\xe2\x9c\x97"));
  REQUIRE_THAT(after, Catch::Matchers::ContainsSubstring("srv-c"));
}

TEST_CASE("empty sink renders header") {
  theme::InitializeTheme(theme::VividPreset());
  McpStatusSink sink;

  auto panel = McpStatusPanelComponent(sink);
  auto output = RenderComponent(panel);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("MCP (0 active"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("0 errors"));
}

TEST_CASE("reconnecting state shows spinner icon") {
  theme::InitializeTheme(theme::VividPreset());
  McpStatusSink sink;
  sink.UpdateServer("srv-x", "reconnecting", "");

  auto panel = McpStatusPanelComponent(sink);
  panel->OnEvent(MakeMouseLeftPress(0, 0));

  auto output = RenderComponent(panel);
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("\xe2\x9f\xb3"));
}

TEST_CASE("keyboard toggle with return") {
  theme::InitializeTheme(theme::VividPreset());
  McpStatusSink sink;
  sink.UpdateServer("srv-a", "ready", "");

  auto panel = McpStatusPanelComponent(sink);
  auto collapsed = RenderComponent(panel);

  REQUIRE(panel->OnEvent(ftxui::Event::Return));

  auto expanded = RenderComponent(panel);
  REQUIRE(collapsed != expanded);
  REQUIRE_THAT(expanded, Catch::Matchers::ContainsSubstring("srv-a"));
}
