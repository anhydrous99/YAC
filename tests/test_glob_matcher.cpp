#include "tool_call/glob_matcher.hpp"

#include <catch2/catch_test_macros.hpp>

using yac::tool_call::GlobToRegex;
using yac::tool_call::MatchesGlob;

TEST_CASE("*.cpp matches files in root only", "[glob_matcher]") {
  CHECK(MatchesGlob("main.cpp", "*.cpp"));
  CHECK(MatchesGlob("foo.cpp", "*.cpp"));
  CHECK_FALSE(MatchesGlob("src/main.cpp", "*.cpp"));
  CHECK_FALSE(MatchesGlob("a/b/main.cpp", "*.cpp"));
}

TEST_CASE("**/*.cpp matches at any depth including root", "[glob_matcher]") {
  CHECK(MatchesGlob("main.cpp", "**/*.cpp"));
  CHECK(MatchesGlob("src/main.cpp", "**/*.cpp"));
  CHECK(MatchesGlob("a/b/c/main.cpp", "**/*.cpp"));
  CHECK_FALSE(MatchesGlob("main.hpp", "**/*.cpp"));
}

TEST_CASE("src/* matches one level under src only", "[glob_matcher]") {
  CHECK(MatchesGlob("src/foo", "src/*"));
  CHECK(MatchesGlob("src/bar.cpp", "src/*"));
  CHECK_FALSE(MatchesGlob("src/foo/bar", "src/*"));
  CHECK_FALSE(MatchesGlob("foo", "src/*"));
}

TEST_CASE("src/** matches any depth under src", "[glob_matcher]") {
  CHECK(MatchesGlob("src/foo", "src/**"));
  CHECK(MatchesGlob("src/foo/bar", "src/**"));
  CHECK(MatchesGlob("src/foo/bar/baz", "src/**"));
  CHECK_FALSE(MatchesGlob("other/foo", "src/**"));
}

TEST_CASE("? matches exactly one non-separator character", "[glob_matcher]") {
  CHECK(MatchesGlob("a.txt", "?.txt"));
  CHECK(MatchesGlob("z.txt", "?.txt"));
  CHECK_FALSE(MatchesGlob("ab.txt", "?.txt"));
  CHECK_FALSE(MatchesGlob(".txt", "?.txt"));
  CHECK_FALSE(MatchesGlob("a/b.txt", "?.txt"));
}

TEST_CASE("dot is escaped and matches only literal dot", "[glob_matcher]") {
  CHECK(MatchesGlob("a.b", "a.b"));
  CHECK_FALSE(MatchesGlob("axb", "a.b"));
  CHECK_FALSE(MatchesGlob("a_b", "a.b"));
}

TEST_CASE("square brackets treated as literals", "[glob_matcher]") {
  CHECK(MatchesGlob("[brackets]", "[brackets]"));
  CHECK_FALSE(MatchesGlob("b", "[brackets]"));
  CHECK_FALSE(MatchesGlob("brackets", "[brackets]"));
}

TEST_CASE("empty pattern matches only empty path", "[glob_matcher]") {
  CHECK(MatchesGlob("", ""));
  CHECK_FALSE(MatchesGlob("a", ""));
  CHECK_FALSE(MatchesGlob("/", ""));
}

TEST_CASE("** alone matches any path including nested", "[glob_matcher]") {
  CHECK(MatchesGlob("a", "**"));
  CHECK(MatchesGlob("a/b/c", "**"));
  CHECK(MatchesGlob("", "**"));
}

TEST_CASE("no-wildcard pattern matches exact string only", "[glob_matcher]") {
  CHECK(MatchesGlob("foo/bar.cpp", "foo/bar.cpp"));
  CHECK_FALSE(MatchesGlob("foo/baz.cpp", "foo/bar.cpp"));
  CHECK_FALSE(MatchesGlob("foo/bar.cpx", "foo/bar.cpp"));
  CHECK_FALSE(MatchesGlob("xfoo/bar.cpp", "foo/bar.cpp"));
}

TEST_CASE("GlobToRegex produces anchored regex strings", "[glob_matcher]") {
  std::string re = GlobToRegex("*.cpp");
  CHECK(re.front() == '^');
  CHECK(re.back() == '$');
}

TEST_CASE("regex metacharacters in pattern are escaped", "[glob_matcher]") {
  CHECK(MatchesGlob("a+b", "a+b"));
  CHECK_FALSE(MatchesGlob("aab", "a+b"));
  CHECK(MatchesGlob("a|b", "a|b"));
  CHECK_FALSE(MatchesGlob("a", "a|b"));
}

TEST_CASE("**/ at start matches zero segments (root file)", "[glob_matcher]") {
  CHECK(MatchesGlob("README.md", "**/README.md"));
  CHECK(MatchesGlob("docs/README.md", "**/README.md"));
  CHECK(MatchesGlob("a/b/c/README.md", "**/README.md"));
  CHECK_FALSE(MatchesGlob("README.txt", "**/README.md"));
}
