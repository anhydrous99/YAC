#include "tool_call/types.hpp"

#include <variant>

#include <catch2/catch_test_macros.hpp>

using namespace yac::tool_call;

TEST_CASE("Tool call structs preserve field values") {
  BashCall bash{"echo hi", "ok", 0, false};
  REQUIRE(bash.command == "echo hi");
  REQUIRE(bash.output == "ok");
  REQUIRE(bash.exit_code == 0);
  REQUIRE_FALSE(bash.is_error);

  FileEditCall edit{"/tmp/file.txt",
                    {{DiffLine::Add, "+line"}, {DiffLine::Context, "line"}}};
  REQUIRE(edit.filepath == "/tmp/file.txt");
  REQUIRE(edit.diff.size() == 2);
  REQUIRE(edit.diff[0].type == DiffLine::Add);
  REQUIRE(edit.diff[0].content == "+line");

  FileReadCall read{"/tmp/file.txt", 12, "excerpt"};
  REQUIRE(read.filepath == "/tmp/file.txt");
  REQUIRE(read.lines_loaded == 12);
  REQUIRE(read.excerpt == "excerpt");

  GrepCall grep{"needle", 2, {{"a.txt", 4, "match"}}};
  REQUIRE(grep.pattern == "needle");
  REQUIRE(grep.match_count == 2);
  REQUIRE(grep.matches.size() == 1);
  REQUIRE(grep.matches[0].filepath == "a.txt");

  GlobCall glob{"*.cpp", {"src/a.cpp", "src/b.cpp"}};
  REQUIRE(glob.pattern == "*.cpp");
  REQUIRE(glob.matched_files.size() == 2);

  WebFetchCall fetch{"https://example.com", "Example", "summary"};
  REQUIRE(fetch.url == "https://example.com");
  REQUIRE(fetch.title == "Example");
  REQUIRE(fetch.excerpt == "summary");

  WebSearchCall search{"query", {{"Title", "https://a", "snip"}}};
  REQUIRE(search.query == "query");
  REQUIRE(search.results.size() == 1);
  REQUIRE(search.results[0].title == "Title");
}

TEST_CASE("ToolCallBlock stores all tool call variants") {
  ToolCallBlock block = BashCall{"cmd", "out", 1, true};
  REQUIRE(std::holds_alternative<BashCall>(block));

  block = FileEditCall{"file", {{DiffLine::Remove, "-old"}}};
  REQUIRE(std::holds_alternative<FileEditCall>(block));

  block = FileReadCall{"file", 3, "text"};
  REQUIRE(std::holds_alternative<FileReadCall>(block));

  block = GrepCall{"pattern", 1, {}};
  REQUIRE(std::holds_alternative<GrepCall>(block));

  block = GlobCall{"pattern", {"file"}};
  REQUIRE(std::holds_alternative<GlobCall>(block));

  block = WebFetchCall{"url", "title", "excerpt"};
  REQUIRE(std::holds_alternative<WebFetchCall>(block));

  block = WebSearchCall{"query", {}};
  REQUIRE(std::holds_alternative<WebSearchCall>(block));
}
