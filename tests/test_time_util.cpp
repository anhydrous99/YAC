#include "presentation/util/time_util.hpp"

#include <chrono>

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation::util;

TEST_CASE("0 seconds ago renders as just now") {
  auto now = std::chrono::system_clock::now();
  REQUIRE(FormatRelativeTime(now) == "just now");
}

TEST_CASE("30 seconds ago renders as just now") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(30);
  REQUIRE(FormatRelativeTime(tp) == "just now");
}

TEST_CASE("90 seconds ago renders as 1m ago") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(90);
  REQUIRE(FormatRelativeTime(tp) == "1m ago");
}

TEST_CASE("150 seconds ago renders as 2m ago") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(150);
  REQUIRE(FormatRelativeTime(tp) == "2m ago");
}

TEST_CASE("3600 seconds ago renders as 1h ago") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(3600);
  REQUIRE(FormatRelativeTime(tp) == "1h ago");
}

TEST_CASE("86400 seconds ago renders as 1d ago") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(86400);
  REQUIRE(FormatRelativeTime(tp) == "1d ago");
}

TEST_CASE("Future timestamp renders as just now") {
  auto tp = std::chrono::system_clock::now() + std::chrono::seconds(60);
  REQUIRE(FormatRelativeTime(tp) == "just now");
}
