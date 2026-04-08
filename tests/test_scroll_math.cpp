#include "presentation/util/scroll_math.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation::util;

TEST_CASE("ShouldShowScrollbar returns false when content is zero") {
  REQUIRE(ShouldShowScrollbar(0, 10) == false);
}

TEST_CASE("ShouldShowScrollbar returns false when content equals viewport") {
  REQUIRE(ShouldShowScrollbar(10, 10) == false);
}

TEST_CASE("ShouldShowScrollbar returns true when content exceeds viewport") {
  REQUIRE(ShouldShowScrollbar(11, 10) == true);
}

TEST_CASE("ShouldShowScrollbar returns true for large content small viewport") {
  REQUIRE(ShouldShowScrollbar(100, 1) == true);
}

TEST_CASE("CalculateThumbSize proportional 100/10/20 gives 2") {
  REQUIRE(CalculateThumbSize(100, 10, 20) == 2);
}

TEST_CASE("CalculateThumbSize enforces minimum of 1") {
  REQUIRE(CalculateThumbSize(1000, 10, 20) == 1);
}

TEST_CASE("CalculateThumbSize proportional 20/10/10 gives 5") {
  REQUIRE(CalculateThumbSize(20, 10, 10) == 5);
}

TEST_CASE(
    "CalculateThumbSize fills entire track when content equals viewport") {
  REQUIRE(CalculateThumbSize(10, 10, 20) == 20);
}

TEST_CASE("CalculateThumbSize zero content returns minimum 1") {
  REQUIRE(CalculateThumbSize(0, 10, 20) == 1);
}

TEST_CASE("CalculateThumbPosition at top returns 0") {
  REQUIRE(CalculateThumbPosition(0, 1000, 20, 2) == 0);
}

TEST_CASE("CalculateThumbPosition at sentinel bottom") {
  REQUIRE(CalculateThumbPosition(10000, 1000, 20, 2) == 18);
}

TEST_CASE("CalculateThumbPosition at middle-ish") {
  REQUIRE(CalculateThumbPosition(500, 1000, 20, 2) == 9);
}

TEST_CASE("CalculateThumbPosition zero content returns 0") {
  REQUIRE(CalculateThumbPosition(500, 0, 20, 2) == 0);
}

TEST_CASE("CalculateScrollFocusFromRatio zero ratio gives 0") {
  REQUIRE(CalculateScrollFocusFromRatio(0.0F, 1000) == 0);
}

TEST_CASE("CalculateScrollFocusFromRatio full ratio gives content height") {
  REQUIRE(CalculateScrollFocusFromRatio(1.0F, 1000) == 1000);
}

TEST_CASE("CalculateScrollFocusFromRatio half ratio gives half content") {
  REQUIRE(CalculateScrollFocusFromRatio(0.5F, 1000) == 500);
}

TEST_CASE("CalculateScrollFocusFromRatio zero content gives 0") {
  REQUIRE(CalculateScrollFocusFromRatio(0.0F, 0) == 0);
}

TEST_CASE("CalculateScrollRatio zero focus gives 0") {
  REQUIRE(CalculateScrollRatio(0, 1000) == 0.0F);
}

TEST_CASE("CalculateScrollRatio sentinel focus gives 1") {
  REQUIRE(CalculateScrollRatio(10000, 1000) == 1.0F);
}

TEST_CASE("CalculateScrollRatio half focus gives approx 0.5") {
  float result = CalculateScrollRatio(500, 1000);
  REQUIRE(result == Catch::Approx(0.5F));
}

TEST_CASE("CalculateScrollRatio zero content gives 0") {
  REQUIRE(CalculateScrollRatio(0, 0) == 0.0F);
}

TEST_CASE("CalculateScrollRatio sentinel with zero content gives 1") {
  REQUIRE(CalculateScrollRatio(10000, 0) == 1.0F);
}
