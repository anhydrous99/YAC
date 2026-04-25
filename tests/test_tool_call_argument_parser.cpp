#include "chat/tool_call_argument_parser.hpp"
#include "tool_call/executor_arguments.hpp"

#include <catch2/catch_test_macros.hpp>

using yac::chat::ExtractStringFieldPartial;
using yac::tool_call::OptionalBool;

TEST_CASE("OptionalBool returns default when key missing") {
  REQUIRE(OptionalBool({}, "replace_all", true));
  REQUIRE_FALSE(OptionalBool({}, "replace_all", false));
}

TEST_CASE("OptionalBool returns true when present true") {
  const auto args = yac::tool_call::Json::parse(R"({"replace_all":true})");
  REQUIRE(OptionalBool(args, "replace_all", false));
}

TEST_CASE("OptionalBool returns false when present false") {
  const auto args = yac::tool_call::Json::parse(R"({"replace_all":false})");
  REQUIRE_FALSE(OptionalBool(args, "replace_all", true));
}

TEST_CASE("OptionalBool returns default for non-bool") {
  const auto args = yac::tool_call::Json::parse(R"({"replace_all":"true"})");
  REQUIRE(OptionalBool(args, "replace_all", true));
  REQUIRE_FALSE(OptionalBool(args, "replace_all", false));
}

TEST_CASE("ExtractStringFieldPartial returns nullopt when key missing") {
  REQUIRE_FALSE(
      ExtractStringFieldPartial(R"({"filepath":"foo")", "content").has_value());
  REQUIRE_FALSE(ExtractStringFieldPartial("", "content").has_value());
  REQUIRE_FALSE(ExtractStringFieldPartial("{", "content").has_value());
}

TEST_CASE("ExtractStringFieldPartial returns value when terminated") {
  const auto result = ExtractStringFieldPartial(
      R"({"filepath":"foo.cpp","content":"hello"})", "content");
  REQUIRE(result.has_value());
  REQUIRE(*result == "hello");
}

TEST_CASE("ExtractStringFieldPartial returns partial while streaming") {
  const auto result = ExtractStringFieldPartial(
      R"({"filepath":"foo.cpp","content":"line1\nline)", "content");
  REQUIRE(result.has_value());
  REQUIRE(*result == "line1\nline");
}

TEST_CASE("ExtractStringFieldPartial decodes escapes") {
  const auto result =
      ExtractStringFieldPartial(R"({"content":"a\"b\\c\nd\te"})", "content");
  REQUIRE(result.has_value());
  REQUIRE(*result == "a\"b\\c\nd\te");
}

TEST_CASE("ExtractStringFieldPartial stops cleanly on truncated \\uXXXX") {
  const auto result =
      ExtractStringFieldPartial(R"({"content":"ok\u00)", "content");
  REQUIRE(result.has_value());
  REQUIRE(*result == "ok");
}

TEST_CASE("ExtractStringFieldPartial handles surrogate pair") {
  // U+1F600 GRINNING FACE
  const auto result =
      ExtractStringFieldPartial(R"({"content":"😀"})", "content");
  REQUIRE(result.has_value());
  REQUIRE(*result == "\xF0\x9F\x98\x80");
}

TEST_CASE("ExtractStringFieldPartial rejects key nested in another value") {
  const auto result =
      ExtractStringFieldPartial(R"({"prev":"a content here"})", "content");
  REQUIRE_FALSE(result.has_value());
}

TEST_CASE("ExtractStringFieldPartial handles keys after other fields") {
  const auto result = ExtractStringFieldPartial(
      R"({"filepath":"a.txt","mode":"w","content":"body)", "content");
  REQUIRE(result.has_value());
  REQUIRE(*result == "body");
}

TEST_CASE("ExtractStringFieldPartial returns empty before value starts") {
  REQUIRE_FALSE(
      ExtractStringFieldPartial(R"({"content)", "content").has_value());
  REQUIRE_FALSE(
      ExtractStringFieldPartial(R"({"content":)", "content").has_value());
  const auto empty_value =
      ExtractStringFieldPartial(R"({"content":"")", "content");
  REQUIRE(empty_value.has_value());
  REQUIRE(empty_value->empty());
}
