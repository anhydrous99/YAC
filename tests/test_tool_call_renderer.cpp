#include "presentation/tool_call/renderer.hpp"
#include "tool_call/types.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation::tool_call;
using namespace yac::tool_call;

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

TEST_CASE("ToolCallRenderer renders file write details") {
  FileWriteCall call{.filepath = "src/new.cpp",
                     .content_preview = "int main() {}\n",
                     .lines_added = 1};

  auto output = RenderToString(call, 80, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("write"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/new.cpp"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Added 1 lines"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("int main()"));
}

TEST_CASE("ToolCallRenderer renders list dir entries") {
  ListDirCall call{"src",
                   {{"presentation", DirectoryEntryType::Directory, 0},
                    {"main.cpp", DirectoryEntryType::File, 42}},
                   false,
                   false,
                   ""};

  auto output = RenderToString(call, 80, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("ls"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("dir"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("main.cpp"));
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

TEST_CASE("ToolCallRenderer renders LSP diagnostics") {
  LspDiagnosticsCall call{
      "src/main.cpp",
      {{DiagnosticSeverity::Error, "expected expression", 12}},
      false,
      ""};

  auto output = RenderToString(call, 80, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("diagnostics"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/main.cpp"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("expected"));
}

TEST_CASE("ToolCallRenderer renders LSP navigation and rename tools") {
  RenderAndCheck(
      LspReferencesCall{
          "main", "src/main.cpp", {{"src/main.cpp", 4, 3}}, false, ""},
      80, 8);
  RenderAndCheck(
      LspGotoDefinitionCall{
          "main", "src/main.cpp", 4, 3, {{"src/main.cpp", 1, 1}}, false, ""},
      80, 8);
  RenderAndCheck(LspRenameCall{"src/main.cpp",
                               4,
                               3,
                               "old",
                               "next",
                               1,
                               {{"src/main.cpp", 4, 3, 4, 6, "next"}},
                               false,
                               ""},
                 80, 10);
  RenderAndCheck(
      LspSymbolsCall{"src/main.cpp", {{"main", "function", 4}}, false, ""}, 80,
      8);
}

TEST_CASE("ToolCallRenderer::BuildGroupSummary empty input yields empty") {
  REQUIRE(ToolCallRenderer::BuildGroupSummary({}).empty());
}

TEST_CASE(
    "ToolCallRenderer::BuildGroupSummary single variant returns one term") {
  BashCall a{"echo", "", 0, false};
  BashCall b{"ls", "", 0, false};
  BashCall c{"pwd", "", 0, false};
  std::vector<ToolCallBlock> blocks{ToolCallBlock{a}, ToolCallBlock{b},
                                    ToolCallBlock{c}};
  std::vector<const ToolCallBlock*> ptrs;
  ptrs.reserve(blocks.size());
  for (const auto& block : blocks) {
    ptrs.push_back(&block);
  }

  REQUIRE(ToolCallRenderer::BuildGroupSummary(ptrs) == "3 bash");
}

TEST_CASE(
    "ToolCallRenderer::BuildGroupSummary orders terms by descending count") {
  std::vector<ToolCallBlock> blocks;
  for (int i = 0; i < 5; ++i) {
    blocks.emplace_back(BashCall{"echo", "", 0, false});
  }
  for (int i = 0; i < 4; ++i) {
    blocks.emplace_back(FileReadCall{"a.txt", 1, ""});
  }
  for (int i = 0; i < 3; ++i) {
    blocks.emplace_back(FileEditCall{"a.txt", {}});
  }
  std::vector<const ToolCallBlock*> ptrs;
  ptrs.reserve(blocks.size());
  for (const auto& b : blocks) {
    ptrs.push_back(&b);
  }

  REQUIRE(ToolCallRenderer::BuildGroupSummary(ptrs) ==
          "5 bash \xc2\xb7 4 read \xc2\xb7 3 edit");
}

TEST_CASE("ToolCallRenderer::BuildGroupSummary collapses LSP variants") {
  std::vector<ToolCallBlock> blocks;
  blocks.emplace_back(LspDiagnosticsCall{"f", {}, false, ""});
  blocks.emplace_back(LspReferencesCall{"s", "f", {}, false, ""});
  blocks.emplace_back(LspRenameCall{"f", 0, 0, "", "", 0, {}, false, ""});
  std::vector<const ToolCallBlock*> ptrs;
  ptrs.reserve(blocks.size());
  for (const auto& block : blocks) {
    ptrs.push_back(&block);
  }

  REQUIRE(ToolCallRenderer::BuildGroupSummary(ptrs) == "3 lsp");
}

TEST_CASE(
    "ToolCallRenderer::BuildGroupSummary caps at four terms and appends "
    "ellipsis when more variants exist") {
  std::vector<ToolCallBlock> blocks;
  blocks.emplace_back(BashCall{"c", "", 0, false});
  blocks.emplace_back(FileReadCall{"r", 1, ""});
  blocks.emplace_back(FileEditCall{"e", {}});
  blocks.emplace_back(FileWriteCall{.filepath = "w"});
  blocks.emplace_back(GrepCall{"g", 0, {}});
  blocks.emplace_back(GlobCall{"*", {}});
  std::vector<const ToolCallBlock*> ptrs;
  for (const auto& b : blocks) {
    ptrs.push_back(&b);
  }

  const auto summary = ToolCallRenderer::BuildGroupSummary(ptrs);

  // Six variants, all count 1 — alphabetical tie-break; top four, then
  // ellipsis.
  REQUIRE(summary ==
          "1 bash \xc2\xb7 1 edit \xc2\xb7 1 glob \xc2\xb7 1 grep "
          "\xc2\xb7 \xe2\x80\xa6");
}

TEST_CASE("ToolCallRenderer handles all tool call variants without crashing") {
  RenderAndCheck(BashCall{"pwd", "", 0, false}, 80, 8);
  RenderAndCheck(FileEditCall{"file.txt", {}}, 80, 8);
  RenderAndCheck(FileReadCall{"file.txt", 1, "line"}, 80, 8);
  RenderAndCheck(
      FileWriteCall{
          .filepath = "file.txt", .content_preview = "line", .lines_added = 1},
      80, 8);
  RenderAndCheck(ListDirCall{"src", {}, false, false, ""}, 80, 8);
  RenderAndCheck(GrepCall{"pattern", 0, {}}, 80, 8);
  RenderAndCheck(GlobCall{"*.cpp", {}}, 80, 8);
  RenderAndCheck(WebFetchCall{"https://example.com", "", ""}, 80, 8);
  RenderAndCheck(WebSearchCall{"query", {}}, 80, 8);
  RenderAndCheck(LspDiagnosticsCall{"file.txt", {}, false, ""}, 80, 8);
  RenderAndCheck(LspReferencesCall{"symbol", "file.txt", {}, false, ""}, 80, 8);
  RenderAndCheck(
      LspGotoDefinitionCall{"symbol", "file.txt", 1, 1, {}, false, ""}, 80, 8);
  RenderAndCheck(
      LspRenameCall{"file.txt", 1, 1, "old", "new", 0, {}, false, ""}, 80, 8);
  RenderAndCheck(LspSymbolsCall{"file.txt", {}, false, ""}, 80, 8);
}
