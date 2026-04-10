#include "presentation/tool_call/renderer.hpp"
#include "presentation/tool_call/types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation::tool_call;

namespace {

std::string RenderToString(const ToolCallBlock& block, int width = 80,
                           int height = 24) {
  auto elem = ToolCallRenderer::Render(block);
  REQUIRE(elem != nullptr);
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, elem);
  return screen.ToString();
}

void RenderAndCheck(const ToolCallBlock& block, int width = 80,
                    int height = 24) {
  auto output = RenderToString(block, width, height);
  REQUIRE_FALSE(output.empty());
}

}  // namespace

TEST_CASE("ToolCallRenderer renders bash command details") {
  BashCall call{"git status", "On branch main", 0, false};

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("#"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("bash"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("git status"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("On branch main"));
}

TEST_CASE(
    "ToolCallRenderer renders bash command responsively and handles errors") {
  BashCall call{"cmake --build build", "build failed", 2, true};

  auto output = RenderToString(call, 40, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("bash"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("cmake"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Exit code: 2"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("build failed"));
}

TEST_CASE("ToolCallRenderer renders file edit details") {
  FileEditCall call{"src/main.cpp",
                    {{DiffLine::Add, "int added = 1;"},
                     {DiffLine::Remove, "int removed = 0;"},
                     {DiffLine::Context, "return 0;"}}};

  auto output = RenderToString(call, 80, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("→"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("edit"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/main.cpp"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("+ int added = 1;"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("- int removed = 0;"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("  return 0;"));
}

TEST_CASE("ToolCallRenderer renders empty file edit at narrow width") {
  FileEditCall call{"src/presentation/tool_call/renderer.cpp", {}};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("edit"));
  REQUIRE_THAT(
      output, Catch::Matchers::ContainsSubstring("src/presentation/tool_call"));
}

TEST_CASE("ToolCallRenderer renders file read details") {
  FileReadCall call{"README.md", 12, "Yet Another Chat"};

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("◆"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("read"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("README.md"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Loaded 12 lines"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Yet Another Chat"));
}

TEST_CASE("ToolCallRenderer renders minimal file read at narrow width") {
  FileReadCall call{"notes.txt", 0, ""};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("read"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("notes.txt"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Loaded 0 lines"));
}

TEST_CASE("ToolCallRenderer renders grep details") {
  GrepCall call{"needle",
                2,
                {{"src/main.cpp", 3, "// needle: cleanup"},
                 {"tests/test.cpp", 9, "needle renderer"}}};

  auto output = RenderToString(call, 80, 12);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("⊕"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("grep"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("needle"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("2 matches"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/main.cpp:3"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("needle renderer"));
}

TEST_CASE("ToolCallRenderer renders empty grep at narrow width") {
  GrepCall call{"needle", 0, {}};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("grep"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("needle"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("0 matches"));
}

TEST_CASE("ToolCallRenderer renders glob details") {
  GlobCall call{"src/**/*.cpp",
                {"src/main.cpp", "src/presentation/message_renderer.cpp"}};

  auto output = RenderToString(call, 80, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("⊙"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("glob"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/**/*.cpp"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/main.cpp"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring(
                           "src/presentation/message_renderer.cpp"));
}

TEST_CASE("ToolCallRenderer renders empty glob at narrow width") {
  GlobCall call{"tests/*.cpp", {}};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("glob"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("tests/*.cpp"));
}

TEST_CASE("ToolCallRenderer renders web fetch details") {
  WebFetchCall call{"https://example.com", "Example Domain",
                    "This domain is for use in examples."};

  auto output = RenderToString(call, 80, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("↗"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("fetch"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("https://example.com"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Example Domain"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("This domain is for use"));
}

TEST_CASE("ToolCallRenderer renders minimal web fetch at narrow width") {
  WebFetchCall call{"https://yac.dev", "", ""};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("fetch"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("https://yac.dev"));
}

TEST_CASE("ToolCallRenderer renders web search details") {
  WebSearchCall call{"yac terminal ui",
                     {{"YAC", "https://yac.dev", "Terminal chat UI"},
                      {"FTXUI", "https://github.com/ArthurSonzogni/FTXUI",
                       "Functional terminal UI"}}};

  auto output = RenderToString(call, 80, 14);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("◎"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("search"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("yac terminal ui"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("YAC"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("https://yac.dev"));
  REQUIRE_THAT(output,
               Catch::Matchers::ContainsSubstring("Functional terminal UI"));
}

TEST_CASE("ToolCallRenderer renders empty web search at narrow width") {
  WebSearchCall call{"docs search", {}};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("search"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("docs search"));
}

TEST_CASE("ToolCallRenderer handles all tool call variants without crashing") {
  RenderAndCheck(BashCall{"pwd", "", 0, false}, 80, 8);
  RenderAndCheck(FileEditCall{"file.txt", {}}, 80, 8);
  RenderAndCheck(FileReadCall{"file.txt", 1, "line"}, 80, 8);
  RenderAndCheck(GrepCall{"pattern", 0, {}}, 80, 8);
  RenderAndCheck(GlobCall{"*.cpp", {}}, 80, 8);
  RenderAndCheck(WebFetchCall{"https://example.com", "", ""}, 80, 8);
  RenderAndCheck(WebSearchCall{"query", {}}, 80, 8);
}
