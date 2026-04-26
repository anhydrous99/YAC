#include "mcp/sse_parser.hpp"

#include <string>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace yac::mcp::test {

TEST_CASE("parses_simple_event") {
  SseParser parser;

  const auto events = parser.FeedChunk("data: hello\n\n");

  REQUIRE(events.size() == 1);
  REQUIRE(events[0].data == "hello");
  REQUIRE(events[0].id.empty());
  REQUIRE(events[0].event.empty());
  REQUIRE(events[0].retry == -1);
}

TEST_CASE("parses_multi_line_data") {
  SseParser parser;

  const auto events = parser.FeedChunk(
      "id: evt-1\n"
      "event: message\n"
      "data: first line\n"
      "data: second line\n"
      "retry: 2500\n\n");

  REQUIRE(events.size() == 1);
  REQUIRE(events[0].id == "evt-1");
  REQUIRE(events[0].event == "message");
  REQUIRE(events[0].data == "first line\nsecond line");
  REQUIRE(events[0].retry == 2500);
}

TEST_CASE("handles_malformed_input") {
  SseParser parser;

  REQUIRE(parser.FeedChunk("\n").empty());
  REQUIRE(parser.FeedChunk("data: par").empty());

  const auto events = parser.FeedChunk(
      "tial\n"
      "unknown: ignored\n"
      "retry: nope\n"
      "\n"
      "\n");

  REQUIRE(events.size() == 1);
  REQUIRE(events[0].data == "partial");
  REQUIRE(events[0].retry == -1);
}

}  // namespace yac::mcp::test
