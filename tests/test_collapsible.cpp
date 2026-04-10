#include "presentation/collapsible.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/component/mouse.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;

namespace {

ftxui::Component TextContent(std::string text) {
  return ftxui::Renderer(
      [text = std::move(text)] { return ftxui::text(text); });
}

ftxui::Component EmptyContent() {
  return ftxui::Renderer([] { return ftxui::emptyElement(); });
}

std::string RenderComponent(const ftxui::Component& component, int width = 40,
                            int height = 10) {
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

ftxui::Event MakeMouseRightPress(int x, int y) {
  ftxui::Mouse mouse;
  mouse.button = ftxui::Mouse::Right;
  mouse.motion = ftxui::Mouse::Pressed;
  mouse.x = x;
  mouse.y = y;
  return ftxui::Event::Mouse("", mouse);
}

ftxui::Event MakeMouseRelease(int x, int y) {
  ftxui::Mouse mouse;
  mouse.button = ftxui::Mouse::Left;
  mouse.motion = ftxui::Mouse::Released;
  mouse.x = x;
  mouse.y = y;
  return ftxui::Event::Mouse("", mouse);
}

}  // namespace

TEST_CASE("Collapsible renders header when collapsed") {
  bool expanded = false;
  auto component =
      Collapsible("Details", TextContent("hidden content"), &expanded);

  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Details"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("▶"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("hidden content"));
}

TEST_CASE("Collapsible renders content when expanded") {
  bool expanded = true;
  auto component =
      Collapsible("Details", TextContent("visible content"), &expanded);

  auto output = RenderComponent(component, 60, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("▼"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("visible content"));
}

TEST_CASE("Collapsible uses caller-provided default expanded state") {
  bool collapsed = false;
  bool expanded = true;
  auto collapsed_component =
      Collapsible("Collapsed", TextContent("hidden"), &collapsed);
  auto expanded_component =
      Collapsible("Expanded", TextContent("shown"), &expanded);

  auto collapsed_output = RenderComponent(collapsed_component);
  auto expanded_output = RenderComponent(expanded_component);

  REQUIRE_THAT(collapsed_output, !Catch::Matchers::ContainsSubstring("hidden"));
  REQUIRE_THAT(expanded_output, Catch::Matchers::ContainsSubstring("shown"));
}

TEST_CASE("Collapsible toggles expanded on mouse left press on header") {
  bool expanded = false;
  auto component = Collapsible("Toggle", TextContent("body"), &expanded);

  RenderComponent(component);

  REQUIRE(component->OnEvent(MakeMouseLeftPress(0, 0)));
  REQUIRE(expanded);
}

TEST_CASE("Collapsible toggles collapsed on second click") {
  bool expanded = true;
  auto component = Collapsible("Toggle", TextContent("body"), &expanded);

  RenderComponent(component);

  REQUIRE(component->OnEvent(MakeMouseLeftPress(0, 0)));
  REQUIRE_FALSE(expanded);
}

TEST_CASE("Collapsible ignores left mouse press outside reflected header") {
  bool expanded = false;
  auto component = Collapsible("Toggle", TextContent("body"), &expanded);

  RenderComponent(component);

  REQUIRE_FALSE(component->OnEvent(MakeMouseLeftPress(100, 100)));
  REQUIRE_FALSE(expanded);
}

TEST_CASE("Collapsible ignores non-left mouse button") {
  bool expanded = false;
  auto component = Collapsible("Toggle", TextContent("body"), &expanded);

  RenderComponent(component);

  REQUIRE_FALSE(component->OnEvent(MakeMouseRightPress(0, 0)));
  REQUIRE_FALSE(expanded);
}

TEST_CASE("Collapsible ignores mouse release") {
  bool expanded = false;
  auto component = Collapsible("Toggle", TextContent("body"), &expanded);

  RenderComponent(component);

  REQUIRE_FALSE(component->OnEvent(MakeMouseRelease(0, 0)));
  REQUIRE_FALSE(expanded);
}

TEST_CASE("Collapsible ignores keyboard events") {
  bool expanded = false;
  auto component = Collapsible("Toggle", TextContent("body"), &expanded);

  REQUIRE_FALSE(component->OnEvent(ftxui::Event::Return));
  REQUIRE_FALSE(component->OnEvent(ftxui::Event::Character(' ')));
  REQUIRE_FALSE(expanded);
}

TEST_CASE("Collapsible with empty content renders header") {
  bool expanded = true;
  auto component = Collapsible("Empty", EmptyContent(), &expanded);

  auto output = RenderComponent(component);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Empty"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("▼"));
}

TEST_CASE("Collapsible independent state for two instances") {
  bool expanded_a = false;
  bool expanded_b = true;
  auto component_a = Collapsible("A", TextContent("content A"), &expanded_a);
  auto component_b = Collapsible("B", TextContent("content B"), &expanded_b);

  RenderComponent(component_a);
  RenderComponent(component_b);

  REQUIRE(component_a->OnEvent(MakeMouseLeftPress(0, 0)));
  REQUIRE(expanded_a);
  REQUIRE(expanded_b);

  REQUIRE(component_b->OnEvent(MakeMouseLeftPress(0, 0)));
  REQUIRE(expanded_a);
  REQUIRE_FALSE(expanded_b);
}

TEST_CASE("Collapsible returns non-null component") {
  bool expanded = false;
  auto component = Collapsible("Test", TextContent("body"), &expanded);

  REQUIRE(component != nullptr);
}
