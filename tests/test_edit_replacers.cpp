#include "tool_call/edit_replacers.hpp"

#include <concepts>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

using namespace yac::tool_call;

namespace {

constexpr std::string_view kMultipleMatchesError =
    "Found multiple matches for oldString. Provide more surrounding lines in "
    "oldString to identify the correct match.";
constexpr std::string_view kOldStringNotFoundError =
    "oldString not found in content.";

void RequireRuntimeErrorMessage(const std::invocable auto& fn,
                                std::string_view expected) {
  try {
    fn();
    FAIL("Expected std::runtime_error");
  } catch (const std::runtime_error& error) {
    REQUIRE(std::string(error.what()) == expected);
  }
}

std::vector<DiffLine::Type> DiffTypes(const std::vector<DiffLine>& diff) {
  std::vector<DiffLine::Type> types;
  types.reserve(diff.size());
  for (const auto& line : diff) {
    types.push_back(line.type);
  }
  return types;
}

std::vector<std::string> DiffContents(const std::vector<DiffLine>& diff) {
  std::vector<std::string> contents;
  contents.reserve(diff.size());
  for (const auto& line : diff) {
    contents.push_back(line.content);
  }
  return contents;
}

}  // namespace

TEST_CASE("SimpleReplacer replaces a unique exact match") {
  const auto replaced = SimpleReplacer("alpha\nbeta\ngamma", "beta", "delta");
  REQUIRE(replaced.has_value());
  REQUIRE(*replaced == "alpha\ndelta\ngamma");
}

TEST_CASE("SimpleReplacer returns nullopt when no match exists") {
  REQUIRE_FALSE(SimpleReplacer("alpha\nbeta", "gamma", "delta").has_value());
}

TEST_CASE("SimpleReplacer throws when multiple exact matches exist") {
  RequireRuntimeErrorMessage(
      [] { (void)SimpleReplacer("beta\nalpha\nbeta", "beta", "delta"); },
      kMultipleMatchesError);
}

TEST_CASE("SimpleReplacer supports deletion with empty replacement") {
  const auto replaced = SimpleReplacer("before target after", "target", "");
  REQUIRE(replaced == std::optional<std::string>{"before  after"});
}

TEST_CASE("LineTrimmedReplacer tolerates trailing whitespace drift") {
  const std::string content = "first\nvalue = 1   \nnext();\nlast\n";
  const std::string old_string = "value = 1\nnext();\n";
  const auto replaced =
      LineTrimmedReplacer(content, old_string, "value = 2\nnext();\n");
  REQUIRE(replaced ==
          std::optional<std::string>{"first\nvalue = 2\nnext();\nlast\n"});
}

TEST_CASE("LineTrimmedReplacer returns nullopt when no trimmed window matches") {
  const auto replaced =
      LineTrimmedReplacer("first\nvalue = 1\nlast\n", "value = 2\n", "x\n");
  REQUIRE_FALSE(replaced.has_value());
}

TEST_CASE("LineTrimmedReplacer throws when multiple trimmed windows match") {
  const std::string content = "value = 1   \nnext();\nmid\nvalue = 1\nnext();\n";
  RequireRuntimeErrorMessage(
      [&] {
        (void)LineTrimmedReplacer(content, "value = 1\nnext();\n",
                                  "value = 2\nnext();\n");
      },
      kMultipleMatchesError);
}

TEST_CASE("WhitespaceNormalizedReplacer handles indentation drift") {
  const std::string content = "if (ready) {\n    call(a,\tb);\n}\n";
  const std::string old_string = "if (ready) {\n  call(a, b);\n}\n";
  const auto replaced = WhitespaceNormalizedReplacer(
      content, old_string, "if (ready) {\n  call(updated);\n}\n");
  REQUIRE(replaced ==
          std::optional<std::string>{"if (ready) {\n  call(updated);\n}\n"});
}

TEST_CASE("WhitespaceNormalizedReplacer returns nullopt when normalized text is absent") {
  const auto replaced = WhitespaceNormalizedReplacer(
      "one\n  alpha(beta)\nthree\n", "one\n  gamma(beta)\nthree\n",
      "unused\n");
  REQUIRE_FALSE(replaced.has_value());
}

TEST_CASE("WhitespaceNormalizedReplacer throws when multiple normalized windows match") {
  const std::string content = "x =  1\nx\t=\t1\n";
  RequireRuntimeErrorMessage(
      [&] { (void)WhitespaceNormalizedReplacer(content, "x = 1\n", "x = 2\n"); },
      kMultipleMatchesError);
}

TEST_CASE("ReplaceAll replaces every occurrence") {
  REQUIRE(ReplaceAll("foo bar foo baz foo", "foo", "qux") ==
          "qux bar qux baz qux");
}

TEST_CASE("ReplaceAll throws when no occurrence exists") {
  RequireRuntimeErrorMessage(
      [] { (void)ReplaceAll("alpha\nbeta", "gamma", "delta"); },
      kOldStringNotFoundError);
}

TEST_CASE("ComputeDiff reports pure addition with context") {
  const auto diff = ComputeDiff("one\nthree\n", "one\ntwo\nthree\n");
  REQUIRE(DiffTypes(diff) ==
          std::vector<DiffLine::Type>{DiffLine::Context, DiffLine::Add,
                                      DiffLine::Context});
  REQUIRE(DiffContents(diff) ==
          std::vector<std::string>{"one", "two", "three"});
}

TEST_CASE("ComputeDiff reports pure deletion with context") {
  const auto diff = ComputeDiff("one\ntwo\nthree\n", "one\nthree\n");
  REQUIRE(DiffTypes(diff) ==
          std::vector<DiffLine::Type>{DiffLine::Context, DiffLine::Remove,
                                      DiffLine::Context});
  REQUIRE(DiffContents(diff) ==
          std::vector<std::string>{"one", "two", "three"});
}

TEST_CASE("ComputeDiff reports modification as remove then add") {
  const auto diff = ComputeDiff("one\ntwo\nthree\n", "one\nTWO\nthree\n");
  REQUIRE(DiffTypes(diff) ==
          std::vector<DiffLine::Type>{DiffLine::Context, DiffLine::Remove,
                                      DiffLine::Add, DiffLine::Context});
  REQUIRE(DiffContents(diff) ==
          std::vector<std::string>{"one", "two", "TWO", "three"});
}

TEST_CASE("ComputeDiff returns all context lines when texts are equal") {
  const auto diff = ComputeDiff("one\ntwo\nthree\n", "one\ntwo\nthree\n");
  REQUIRE(DiffTypes(diff) ==
          std::vector<DiffLine::Type>{DiffLine::Context, DiffLine::Context,
                                      DiffLine::Context});
  REQUIRE(DiffContents(diff) ==
          std::vector<std::string>{"one", "two", "three"});
}

TEST_CASE("ComputeDiff keeps exactly three context lines around distant regions") {
  const std::string old_text =
      "l1\nl2\nl3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nl12\nl13\nl14\nl15\n";
  const std::string new_text =
      "l1\nl2\nL3\nl4\nl5\nl6\nl7\nl8\nl9\nl10\nl11\nL12\nl13\nl14\nl15\n";
  const auto diff = ComputeDiff(old_text, new_text);

  REQUIRE(DiffContents(diff) ==
          std::vector<std::string>{"l1",  "l2",  "l3",  "L3",  "l4",
                                   "l5",  "l6",  "l9",  "l10", "l11",
                                   "l12", "L12", "l13", "l14", "l15"});
  REQUIRE(DiffTypes(diff) ==
          std::vector<DiffLine::Type>{
              DiffLine::Context, DiffLine::Context, DiffLine::Remove,
              DiffLine::Add,     DiffLine::Context, DiffLine::Context,
              DiffLine::Context, DiffLine::Context, DiffLine::Context,
              DiffLine::Context, DiffLine::Remove,  DiffLine::Add,
              DiffLine::Context, DiffLine::Context, DiffLine::Context});
}
