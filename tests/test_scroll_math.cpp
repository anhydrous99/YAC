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
  REQUIRE(CalculateThumbPosition(0, 1000, 100, 20, 2) == 0);
}

TEST_CASE("CalculateThumbPosition at bottom") {
  REQUIRE(CalculateThumbPosition(900, 1000, 100, 20, 2) == 18);
}

TEST_CASE("CalculateThumbPosition at middle") {
  REQUIRE(CalculateThumbPosition(450, 1000, 100, 20, 2) == 9);
}

TEST_CASE("CalculateThumbPosition zero content returns 0") {
  REQUIRE(CalculateThumbPosition(500, 0, 100, 20, 2) == 0);
}

TEST_CASE("CalculateMaxScrollOffset returns content minus viewport") {
  REQUIRE(CalculateMaxScrollOffset(1000, 100) == 900);
}

TEST_CASE("CalculateMaxScrollOffset returns 0 when content fits viewport") {
  REQUIRE(CalculateMaxScrollOffset(100, 100) == 0);
}

TEST_CASE("ClampScrollOffset clamps below top") {
  REQUIRE(ClampScrollOffset(-10, 1000, 100) == 0);
}

TEST_CASE("ClampScrollOffset clamps above bottom") {
  REQUIRE(ClampScrollOffset(10000, 1000, 100) == 900);
}

TEST_CASE("CalculateFrameFocusY compensates for frame centering") {
  REQUIRE(CalculateFrameFocusY(30, 11) == 35);
}

TEST_CASE("CalculateScrollOffsetFromRatio zero ratio gives 0") {
  REQUIRE(CalculateScrollOffsetFromRatio(0.0F, 1000, 100) == 0);
}

TEST_CASE("CalculateScrollOffsetFromRatio full ratio gives max offset") {
  REQUIRE(CalculateScrollOffsetFromRatio(1.0F, 1000, 100) == 900);
}

TEST_CASE("CalculateScrollOffsetFromRatio half ratio gives half max offset") {
  REQUIRE(CalculateScrollOffsetFromRatio(0.5F, 1000, 100) == 450);
}

TEST_CASE("CalculateScrollOffsetFromRatio zero content gives 0") {
  REQUIRE(CalculateScrollOffsetFromRatio(0.0F, 0, 100) == 0);
}

TEST_CASE("CalculateScrollRatio zero offset gives 0") {
  REQUIRE(CalculateScrollRatio(0, 1000, 100) == 0.0F);
}

TEST_CASE("CalculateScrollRatio bottom offset gives 1") {
  REQUIRE(CalculateScrollRatio(900, 1000, 100) == 1.0F);
}

TEST_CASE("CalculateScrollRatio half offset gives approx 0.5") {
  float result = CalculateScrollRatio(450, 1000, 100);
  REQUIRE(result == Catch::Approx(0.5F));
}

TEST_CASE("CalculateScrollRatio zero content gives 0") {
  REQUIRE(CalculateScrollRatio(0, 0, 100) == 0.0F);
}

TEST_CASE("CalculateScrollRatio fitting content gives 0") {
  REQUIRE(CalculateScrollRatio(10000, 100, 100) == 0.0F);
}
