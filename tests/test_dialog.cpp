#include "presentation/dialog.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/event.hpp>
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation;

namespace {

class EventCatcher : public ftxui::ComponentBase {
 public:
  explicit EventCatcher(bool* handled) : handled_(handled) {}

  ftxui::Element OnRender() override {
    return ftxui::text("interactive child");
  }

  bool OnEvent(ftxui::Event event) override {
    if (event == ftxui::Event::Character('a')) {
      *handled_ = true;
      return true;
    }
    return false;
  }

 private:
  bool* handled_;
};

ftxui::Component StaticText(std::string text) {
  return ftxui::Renderer(
      [text = std::move(text)] { return ftxui::text(text); });
}

std::string RenderComponent(const ftxui::Component& comp, int width = 80,
                            int height = 24) {
  auto screen = ftxui::Screen(width, height);
  ftxui::Render(screen, comp->Render());
  return screen.ToString();
}

}  // namespace

TEST_CASE("DialogPanel returns non-null component") {
  bool show = true;
  auto comp = DialogPanel("Test", StaticText("body"), &show);
  REQUIRE(comp != nullptr);
}

TEST_CASE("DialogPanel renders title with prominent border") {
  bool show = true;
  auto comp = DialogPanel("My Title", StaticText("content"), &show);
  auto output = RenderComponent(comp, 80, 24);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("My Title"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("─"));
}

TEST_CASE("DialogPanel renders inner content when shown") {
  bool show = true;
  auto comp = DialogPanel("Dialog", StaticText("inner body text"), &show);
  auto output = RenderComponent(comp, 80, 24);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("inner body text"));
}

TEST_CASE("DialogPanel hidden state renders nothing and ignores Escape") {
  bool show = false;
  auto comp = DialogPanel("Hidden", StaticText("secret"), &show);
  auto output = RenderComponent(comp, 80, 24);

  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("Hidden"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("secret"));
  REQUIRE_FALSE(comp->OnEvent(ftxui::Event::Escape));
}

TEST_CASE("DialogPanel Escape key sets show to false") {
  bool show = true;
  auto comp = DialogPanel("Dialog", StaticText("content"), &show);

  REQUIRE(comp->OnEvent(ftxui::Event::Escape));
  REQUIRE_FALSE(show);
}

TEST_CASE("DialogPanel forwards non-Escape events to visible child content") {
  bool show = true;
  bool handled = false;
  auto comp = DialogPanel("Dialog", ftxui::Make<EventCatcher>(&handled), &show);

  REQUIRE(comp->OnEvent(ftxui::Event::Character('a')));
  REQUIRE(handled);
  REQUIRE(show);
}

TEST_CASE("DialogPanel caps width below 72 columns") {
  bool show = true;
  auto comp = DialogPanel("Wide", StaticText(std::string(100, 'X')), &show);
  auto output = RenderComponent(comp, 100, 24);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Wide"));
  REQUIRE(output.find(std::string(72, 'X')) == std::string::npos);
}

TEST_CASE("DialogModal renders only main content when hidden") {
  bool show = false;
  auto comp =
      DialogModal(StaticText("main body"), StaticText("modal body"), &show);
  auto output = RenderComponent(comp, 80, 24);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("main body"));
  REQUIRE_THAT(output, !Catch::Matchers::ContainsSubstring("modal body"));
}

TEST_CASE("DialogModal overlays modal content when shown") {
  bool show = true;
  auto comp =
      DialogModal(StaticText("main body"), StaticText("modal body"), &show);
  auto output = RenderComponent(comp, 80, 24);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("main body"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("modal body"));
}

TEST_CASE("DialogModal forwards Escape to visible static dialog panel") {
  bool show = true;
  auto panel = DialogPanel("Help", StaticText("static help body"), &show);
  auto comp = DialogModal(StaticText("main body"), panel, &show);

  REQUIRE(comp->OnEvent(ftxui::Event::Escape));
  REQUIRE_FALSE(show);
}
