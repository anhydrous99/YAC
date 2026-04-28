#include "presentation/util/count_summary.hpp"

#include <catch2/catch_test_macros.hpp>

using yac::presentation::util::CountSummary;

TEST_CASE("CountSummary uses singular when count is 1") {
  REQUIRE(CountSummary(1, "line", "lines") == "1 line");
  REQUIRE(CountSummary(1, "entry", "entries") == "1 entry");
  REQUIRE(CountSummary(1, "match", "matches") == "1 match");
}

TEST_CASE("CountSummary uses plural otherwise") {
  REQUIRE(CountSummary(0, "line", "lines") == "0 lines");
  REQUIRE(CountSummary(2, "line", "lines") == "2 lines");
  REQUIRE(CountSummary(42, "diagnostic", "diagnostics") == "42 diagnostics");
}
