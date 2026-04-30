#include "util/string_util.hpp"

#include <catch2/catch_test_macros.hpp>

using yac::util::SplitLines;
using yac::util::ToLowerAscii;
using yac::util::Trim;
using yac::util::TrimLeft;
using yac::util::TrimLeftSv;
using yac::util::TrimRight;
using yac::util::TrimRightSv;
using yac::util::TrimSv;

TEST_CASE("Trim removes leading and trailing whitespace") {
  REQUIRE(Trim("  hello  ") == "hello");
  REQUIRE(Trim("\t\nhello\r\n") == "hello");
  REQUIRE(Trim("hello") == "hello");
}

TEST_CASE("Trim returns empty for whitespace-only input") {
  REQUIRE(Trim("   ").empty());
  REQUIRE(Trim("\t\r\n\v\f").empty());
  REQUIRE(Trim("").empty());
}

TEST_CASE("TrimLeft only removes leading whitespace") {
  REQUIRE(TrimLeft("  hello  ") == "hello  ");
  REQUIRE(TrimLeft("hello") == "hello");
}

TEST_CASE("TrimRight only removes trailing whitespace") {
  REQUIRE(TrimRight("  hello  ") == "  hello");
  REQUIRE(TrimRight("hello") == "hello");
}

TEST_CASE("TrimSv variants are zero-copy and equivalent") {
  std::string_view s = "  trim me  ";
  REQUIRE(TrimSv(s) == "trim me");
  REQUIRE(TrimLeftSv(s) == "trim me  ");
  REQUIRE(TrimRightSv(s) == "  trim me");
}

TEST_CASE("ToLowerAscii lowercases ASCII letters") {
  REQUIRE(ToLowerAscii("Hello World") == "hello world");
  REQUIRE(ToLowerAscii("ABCDE") == "abcde");
  REQUIRE(ToLowerAscii("123 ABC") == "123 abc");
  REQUIRE(ToLowerAscii("").empty());
}

TEST_CASE("SplitLines splits on newline preserving content") {
  auto lines = SplitLines("a\nb\nc");
  REQUIRE(lines.size() == 3);
  REQUIRE(lines[0] == "a");
  REQUIRE(lines[1] == "b");
  REQUIRE(lines[2] == "c");
}

TEST_CASE("SplitLines yields single empty line for empty input") {
  auto lines = SplitLines("");
  REQUIRE(lines.size() == 1);
  REQUIRE(lines[0].empty());
}

TEST_CASE("SplitLines drops trailing newline") {
  auto lines = SplitLines("first\nsecond\n");
  REQUIRE(lines.size() == 2);
  REQUIRE(lines[0] == "first");
  REQUIRE(lines[1] == "second");
}
