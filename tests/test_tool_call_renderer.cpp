#include "presentation/tool_call/renderer.hpp"
#include "tool_call/types.hpp"

#include <string>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <ftxui/screen/screen.hpp>

using namespace yac::presentation::tool_call;
using namespace yac::tool_call;

namespace {

// Strips ANSI CSI sequences (\e[...m) so substring matches see only the
// visible text. Necessary now that tool-call cards run code through the
// syntax highlighter, which interleaves color escapes between tokens.
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

std::string RenderToString(const ToolCallBlock& block, int width = 80,
                           int height = 24) {
  auto elem = ToolCallRenderer::Render(block);
  REQUIRE(elem != nullptr);
  ftxui::Screen screen(width, height);
  ftxui::Render(screen, elem);
  return StripAnsi(screen.ToString());
}

void RenderAndCheck(const ToolCallBlock& block, int width = 80,
                    int height = 24) {
  auto output = RenderToString(block, width, height);
  REQUIRE_FALSE(output.empty());
}

}  // namespace

TEST_CASE("ToolCallRenderer renders bash command details") {
  BashCall call{.command = "git status",
                .output = "On branch main",
                .exit_code = 0,
                .is_error = false};

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("#"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("bash"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("git status"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("On branch main"));
}

TEST_CASE("ToolCallRenderer renders generic tool errors") {
  ToolCallError call{.tool_name = "does_not_exist",
                     .error = {.message = "Unknown tool: does_not_exist"}};

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("tool"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("does_not_exist"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring(
                           "Error: Unknown tool: does_not_exist"));
  REQUIRE(ToolCallRenderer::BuildSummary(call) ==
          "Unknown tool: does_not_exist");
}

TEST_CASE(
    "ToolCallRenderer renders bash command responsively and handles errors") {
  BashCall call{.command = "cmake --build build",
                .output = "build failed",
                .exit_code = 2,
                .is_error = true};

  auto output = RenderToString(call, 40, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("bash"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("cmake"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Exit code: 2"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("build failed"));
}

TEST_CASE("ToolCallRenderer renders file edit details") {
  FileEditCall call{.filepath = "src/main.cpp",
                    .diff = {{DiffLine::Add, "int added = 1;"},
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
  FileEditCall call{.filepath = "src/presentation/tool_call/renderer.cpp",
                    .diff = {}};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("edit"));
  REQUIRE_THAT(
      output, Catch::Matchers::ContainsSubstring("src/presentation/tool_call"));
}

TEST_CASE("ToolCallRenderer renders file edit errors") {
  FileEditCall call{.filepath = "src/main.cpp", .diff = {}};
  call.is_error = true;
  call.error = "some error message";

  auto output = RenderToString(call, 80, 8);

  REQUIRE_FALSE(output.empty());
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Error: some error"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/main.cpp"));
}

TEST_CASE("ToolCallRenderer renders file read details") {
  FileReadCall call{.filepath = "README.md",
                    .lines_loaded = 12,
                    .excerpt = "Yet Another Chat"};

  auto output = RenderToString(call, 80, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("◆"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("read"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("README.md"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("12 lines"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Yet Another Chat"));
}

TEST_CASE("ToolCallRenderer renders minimal file read at narrow width") {
  FileReadCall call{.filepath = "notes.txt", .lines_loaded = 0, .excerpt = ""};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("read"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("notes.txt"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("0 lines"));
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
  ListDirCall call{
      .path = "src",
      .entries = {{"presentation", DirectoryEntryType::Directory, 0},
                  {"main.cpp", DirectoryEntryType::File, 42}},
      .truncated = false,
      .is_error = false,
      .error = ""};

  auto output = RenderToString(call, 80, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("ls"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("dir"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("main.cpp"));
}

TEST_CASE("ToolCallRenderer renders grep details") {
  GrepCall call{.pattern = "needle",
                .match_count = 2,
                .matches = {{"src/main.cpp", 3, "// needle: cleanup"},
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
  GrepCall call{.pattern = "needle", .match_count = 0, .matches = {}};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("grep"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("needle"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("0 matches"));
}

TEST_CASE("ToolCallRenderer renders grep errors") {
  GrepCall call{.pattern = "needle", .match_count = 0, .matches = {}};
  call.is_error = true;
  call.error = "some error message";

  auto output = RenderToString(call, 80, 8);

  REQUIRE_FALSE(output.empty());
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Error: some error"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("needle"));
}

TEST_CASE("ToolCallRenderer renders glob details") {
  GlobCall call{.pattern = "src/**/*.cpp",
                .matched_files = {"src/main.cpp",
                                  "src/presentation/message_renderer.cpp"}};

  auto output = RenderToString(call, 80, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("⊙"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("glob"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/**/*.cpp"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/main.cpp"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring(
                           "src/presentation/message_renderer.cpp"));
}

TEST_CASE("ToolCallRenderer renders empty glob at narrow width") {
  GlobCall call{.pattern = "tests/*.cpp", .matched_files = {}};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("glob"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("tests/*.cpp"));
}

TEST_CASE("ToolCallRenderer renders glob errors") {
  GlobCall call{.pattern = "tests/*.cpp", .matched_files = {}};
  call.is_error = true;
  call.error = "some error message";

  auto output = RenderToString(call, 80, 8);

  REQUIRE_FALSE(output.empty());
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("Error: some error"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("tests/*.cpp"));
}

TEST_CASE("ToolCallRenderer renders web fetch details") {
  WebFetchCall call{.url = "https://example.com",
                    .title = "Example Domain",
                    .excerpt = "This domain is for use in examples."};

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
  WebFetchCall call{.url = "https://yac.dev", .title = "", .excerpt = ""};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("fetch"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("https://yac.dev"));
}

TEST_CASE("ToolCallRenderer renders web search details") {
  WebSearchCall call{
      .query = "yac terminal ui",
      .results = {{"YAC", "https://yac.dev", "Terminal chat UI"},
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
  WebSearchCall call{.query = "docs search", .results = {}};

  auto output = RenderToString(call, 40, 8);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("search"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("docs search"));
}

TEST_CASE("ToolCallRenderer renders LSP diagnostics") {
  LspDiagnosticsCall call{
      .file_path = "src/main.cpp",
      .diagnostics = {{DiagnosticSeverity::Error, "expected expression", 12}},
      .is_error = false,
      .error = ""};

  auto output = RenderToString(call, 80, 10);

  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("diagnostics"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("src/main.cpp"));
  REQUIRE_THAT(output, Catch::Matchers::ContainsSubstring("expected"));
}

TEST_CASE("ToolCallRenderer renders LSP navigation and rename tools") {
  RenderAndCheck(LspReferencesCall{.symbol = "main",
                                   .file_path = "src/main.cpp",
                                   .references = {{"src/main.cpp", 4, 3}},
                                   .is_error = false,
                                   .error = ""},
                 80, 8);
  RenderAndCheck(LspGotoDefinitionCall{.symbol = "main",
                                       .file_path = "src/main.cpp",
                                       .line = 4,
                                       .character = 3,
                                       .definitions = {{"src/main.cpp", 1, 1}},
                                       .is_error = false,
                                       .error = ""},
                 80, 8);
  RenderAndCheck(
      LspRenameCall{.file_path = "src/main.cpp",
                    .line = 4,
                    .character = 3,
                    .old_name = "old",
                    .new_name = "next",
                    .changes_count = 1,
                    .changes = {{"src/main.cpp", 4, 3, 4, 6, "next"}},
                    .is_error = false,
                    .error = ""},
      80, 10);
  RenderAndCheck(LspSymbolsCall{.file_path = "src/main.cpp",
                                .symbols = {{"main", "function", 4}},
                                .is_error = false,
                                .error = ""},
                 80, 8);
}

TEST_CASE("ToolCallRenderer::BuildGroupSummary empty input yields empty") {
  REQUIRE(ToolCallRenderer::BuildGroupSummary({}).empty());
}

TEST_CASE(
    "ToolCallRenderer::BuildGroupSummary single variant returns one term") {
  BashCall a{
      .command = "echo", .output = "", .exit_code = 0, .is_error = false};
  BashCall b{.command = "ls", .output = "", .exit_code = 0, .is_error = false};
  BashCall c{.command = "pwd", .output = "", .exit_code = 0, .is_error = false};
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
    blocks.emplace_back(BashCall{
        .command = "echo", .output = "", .exit_code = 0, .is_error = false});
  }
  for (int i = 0; i < 4; ++i) {
    blocks.emplace_back(
        FileReadCall{.filepath = "a.txt", .lines_loaded = 1, .excerpt = ""});
  }
  for (int i = 0; i < 3; ++i) {
    blocks.emplace_back(FileEditCall{.filepath = "a.txt", .diff = {}});
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
  blocks.emplace_back(LspDiagnosticsCall{
      .file_path = "f", .diagnostics = {}, .is_error = false, .error = ""});
  blocks.emplace_back(LspReferencesCall{.symbol = "s",
                                        .file_path = "f",
                                        .references = {},
                                        .is_error = false,
                                        .error = ""});
  blocks.emplace_back(LspRenameCall{.file_path = "f",
                                    .line = 0,
                                    .character = 0,
                                    .old_name = "",
                                    .new_name = "",
                                    .changes_count = 0,
                                    .changes = {},
                                    .is_error = false,
                                    .error = ""});
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
  blocks.emplace_back(BashCall{
      .command = "c", .output = "", .exit_code = 0, .is_error = false});
  blocks.emplace_back(
      FileReadCall{.filepath = "r", .lines_loaded = 1, .excerpt = ""});
  blocks.emplace_back(FileEditCall{.filepath = "e", .diff = {}});
  blocks.emplace_back(FileWriteCall{.filepath = "w"});
  blocks.emplace_back(
      GrepCall{.pattern = "g", .match_count = 0, .matches = {}});
  blocks.emplace_back(GlobCall{.pattern = "*", .matched_files = {}});
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
  RenderAndCheck(
      BashCall{
          .command = "pwd", .output = "", .exit_code = 0, .is_error = false},
      80, 8);
  RenderAndCheck(FileEditCall{.filepath = "file.txt", .diff = {}}, 80, 8);
  RenderAndCheck(
      FileReadCall{
          .filepath = "file.txt", .lines_loaded = 1, .excerpt = "line"},
      80, 8);
  RenderAndCheck(
      FileWriteCall{
          .filepath = "file.txt", .content_preview = "line", .lines_added = 1},
      80, 8);
  RenderAndCheck(ListDirCall{.path = "src",
                             .entries = {},
                             .truncated = false,
                             .is_error = false,
                             .error = ""},
                 80, 8);
  RenderAndCheck(
      GrepCall{.pattern = "pattern", .match_count = 0, .matches = {}}, 80, 8);
  RenderAndCheck(GlobCall{.pattern = "*.cpp", .matched_files = {}}, 80, 8);
  RenderAndCheck(
      WebFetchCall{.url = "https://example.com", .title = "", .excerpt = ""},
      80, 8);
  RenderAndCheck(WebSearchCall{.query = "query", .results = {}}, 80, 8);
  RenderAndCheck(LspDiagnosticsCall{.file_path = "file.txt",
                                    .diagnostics = {},
                                    .is_error = false,
                                    .error = ""},
                 80, 8);
  RenderAndCheck(LspReferencesCall{.symbol = "symbol",
                                   .file_path = "file.txt",
                                   .references = {},
                                   .is_error = false,
                                   .error = ""},
                 80, 8);
  RenderAndCheck(LspGotoDefinitionCall{.symbol = "symbol",
                                       .file_path = "file.txt",
                                       .line = 1,
                                       .character = 1,
                                       .definitions = {},
                                       .is_error = false,
                                       .error = ""},
                 80, 8);
  RenderAndCheck(LspRenameCall{.file_path = "file.txt",
                               .line = 1,
                               .character = 1,
                               .old_name = "old",
                               .new_name = "new",
                               .changes_count = 0,
                               .changes = {},
                               .is_error = false,
                               .error = ""},
                 80, 8);
  RenderAndCheck(LspSymbolsCall{.file_path = "file.txt",
                                .symbols = {},
                                .is_error = false,
                                .error = ""},
                 80, 8);
}
