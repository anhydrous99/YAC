#include "tool_call/executor.hpp"
#include "tool_call/types.hpp"

#include <type_traits>
#include <variant>

#include <catch2/catch_test_macros.hpp>

using namespace yac::tool_call;

TEST_CASE("Tool call structs preserve field values") {
  BashCall bash{
      .command = "echo hi", .output = "ok", .exit_code = 0, .is_error = false};
  REQUIRE(bash.command == "echo hi");
  REQUIRE(bash.output == "ok");
  REQUIRE(bash.exit_code == 0);
  REQUIRE_FALSE(bash.is_error);

  FileEditCall edit{
      .filepath = "/tmp/file.txt",
      .diff = {{DiffLine::Add, "+line"}, {DiffLine::Context, "line"}}};
  REQUIRE(edit.filepath == "/tmp/file.txt");
  REQUIRE(edit.diff.size() == 2);
  REQUIRE(edit.diff[0].type == DiffLine::Add);
  REQUIRE(edit.diff[0].content == "+line");

  FileReadCall read{
      .filepath = "/tmp/file.txt", .lines_loaded = 12, .excerpt = "excerpt"};
  REQUIRE(read.filepath == "/tmp/file.txt");
  REQUIRE(read.lines_loaded == 12);
  REQUIRE(read.excerpt == "excerpt");

  FileWriteCall write{.filepath = "/tmp/file.txt",
                      .content_preview = "new text",
                      .lines_added = 1,
                      .lines_removed = 2};
  REQUIRE(write.filepath == "/tmp/file.txt");
  REQUIRE(write.lines_added == 1);
  REQUIRE(write.lines_removed == 2);

  ListDirCall list{.path = "/tmp",
                   .entries = {{"file.txt", DirectoryEntryType::File, 8}},
                   .truncated = false,
                   .is_error = false,
                   .error = ""};
  REQUIRE(list.path == "/tmp");
  REQUIRE(list.entries.size() == 1);
  REQUIRE(list.entries[0].type == DirectoryEntryType::File);

  GrepCall grep{.pattern = "needle",
                .match_count = 2,
                .matches = {{"a.txt", 4, "match"}}};
  REQUIRE(grep.pattern == "needle");
  REQUIRE(grep.match_count == 2);
  REQUIRE(grep.matches.size() == 1);
  REQUIRE(grep.matches[0].filepath == "a.txt");

  GlobCall glob{.pattern = "*.cpp",
                .matched_files = {"src/a.cpp", "src/b.cpp"}};
  REQUIRE(glob.pattern == "*.cpp");
  REQUIRE(glob.matched_files.size() == 2);

  WebFetchCall fetch{
      .url = "https://example.com", .title = "Example", .excerpt = "summary"};
  REQUIRE(fetch.url == "https://example.com");
  REQUIRE(fetch.title == "Example");
  REQUIRE(fetch.excerpt == "summary");

  WebSearchCall search{.query = "query",
                       .results = {{"Title", "https://a", "snip"}}};
  REQUIRE(search.query == "query");
  REQUIRE(search.results.size() == 1);
  REQUIRE(search.results[0].title == "Title");

  LspDiagnosticsCall diagnostics{
      .file_path = "src/main.cpp",
      .diagnostics = {{DiagnosticSeverity::Error, "expected ';'", 4}},
      .is_error = false,
      .error = ""};
  REQUIRE(diagnostics.diagnostics[0].severity == DiagnosticSeverity::Error);

  LspReferencesCall references{.symbol = "main",
                               .file_path = "src/main.cpp",
                               .references = {{"src/main.cpp", 3, 5}},
                               .is_error = false,
                               .error = ""};
  REQUIRE(references.references[0].line == 3);

  LspGotoDefinitionCall definition{.symbol = "main",
                                   .file_path = "src/main.cpp",
                                   .line = 3,
                                   .character = 5,
                                   .definitions = {{"src/main.cpp", 1, 1}},
                                   .is_error = false,
                                   .error = ""};
  REQUIRE(definition.definitions[0].character == 1);

  LspRenameCall rename{.file_path = "src/main.cpp",
                       .line = 3,
                       .character = 5,
                       .old_name = "old",
                       .new_name = "next",
                       .changes_count = 1,
                       .changes = {{"src/main.cpp", 3, 5, 3, 8, "next"}},
                       .is_error = false,
                       .error = ""};
  REQUIRE(rename.changes_count == 1);

  LspSymbolsCall symbols{.file_path = "src/main.cpp",
                         .symbols = {{"main", "function", 3}},
                         .is_error = false,
                         .error = ""};
  REQUIRE(symbols.symbols[0].name == "main");
}

TEST_CASE("ToolCallBlock stores all tool call variants") {
  ToolCallBlock block = BashCall{
      .command = "cmd", .output = "out", .exit_code = 1, .is_error = true};
  REQUIRE(std::holds_alternative<BashCall>(block));

  block =
      FileEditCall{.filepath = "file", .diff = {{DiffLine::Remove, "-old"}}};
  REQUIRE(std::holds_alternative<FileEditCall>(block));

  block =
      FileReadCall{.filepath = "file", .lines_loaded = 3, .excerpt = "text"};
  REQUIRE(std::holds_alternative<FileReadCall>(block));

  block = FileWriteCall{
      .filepath = "file", .content_preview = "text", .lines_added = 1};
  REQUIRE(std::holds_alternative<FileWriteCall>(block));

  block = ListDirCall{.path = "dir",
                      .entries = {},
                      .truncated = false,
                      .is_error = false,
                      .error = ""};
  REQUIRE(std::holds_alternative<ListDirCall>(block));

  block = GrepCall{.pattern = "pattern", .match_count = 1, .matches = {}};
  REQUIRE(std::holds_alternative<GrepCall>(block));

  block = GlobCall{.pattern = "pattern", .matched_files = {"file"}};
  REQUIRE(std::holds_alternative<GlobCall>(block));

  block = WebFetchCall{.url = "url", .title = "title", .excerpt = "excerpt"};
  REQUIRE(std::holds_alternative<WebFetchCall>(block));

  block = WebSearchCall{.query = "query", .results = {}};
  REQUIRE(std::holds_alternative<WebSearchCall>(block));

  block = LspDiagnosticsCall{
      .file_path = "file", .diagnostics = {}, .is_error = false, .error = ""};
  REQUIRE(std::holds_alternative<LspDiagnosticsCall>(block));

  block = LspReferencesCall{.symbol = "symbol",
                            .file_path = "file",
                            .references = {},
                            .is_error = false,
                            .error = ""};
  REQUIRE(std::holds_alternative<LspReferencesCall>(block));

  block = LspGotoDefinitionCall{.symbol = "symbol",
                                .file_path = "file",
                                .line = 1,
                                .character = 1,
                                .definitions = {},
                                .is_error = false,
                                .error = ""};
  REQUIRE(std::holds_alternative<LspGotoDefinitionCall>(block));

  block = LspRenameCall{.file_path = "file",
                        .line = 1,
                        .character = 1,
                        .old_name = "old",
                        .new_name = "new",
                        .changes_count = 0,
                        .changes = {},
                        .is_error = false,
                        .error = ""};
  REQUIRE(std::holds_alternative<LspRenameCall>(block));

  block = LspSymbolsCall{
      .file_path = "file", .symbols = {}, .is_error = false, .error = ""};
  REQUIRE(std::holds_alternative<LspSymbolsCall>(block));

  block = McpToolCall{};
  REQUIRE(std::holds_alternative<McpToolCall>(block));
}

TEST_CASE("MCP core types default construct") {
  McpResultBlock result_block;
  REQUIRE(result_block.kind == McpResultBlockKind::Text);
  REQUIRE(result_block.text.empty());
  REQUIRE(result_block.mime_type.empty());
  REQUIRE(result_block.uri.empty());
  REQUIRE(result_block.name.empty());
  REQUIRE(result_block.bytes == 0);

  McpToolCall call;
  REQUIRE(call.server_id.empty());
  REQUIRE(call.tool_name.empty());
  REQUIRE(call.original_tool_name.empty());
  REQUIRE(call.arguments_json.empty());
  REQUIRE(call.result_blocks.empty());
  REQUIRE_FALSE(call.is_error);
  REQUIRE(call.error.empty());
  REQUIRE_FALSE(call.is_truncated);
  REQUIRE(call.result_bytes == 0);
}

TEST_CASE("ToolExecutionResult is available through core types") {
  static_assert(std::is_same_v<ToolExecutionResult,
                               yac::core_types::ToolExecutionResult>);
  ToolExecutionResult result{.block = McpToolCall{}, .result_json = "{}"};
  REQUIRE(std::holds_alternative<McpToolCall>(result.block));
  REQUIRE(result.result_json == "{}");
}
