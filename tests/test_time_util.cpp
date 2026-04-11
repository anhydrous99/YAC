#include "presentation/util/time_util.hpp"

#include <chrono>
#include <optional>
#include <utility>

#include <catch2/catch_test_macros.hpp>

using namespace yac::presentation::util;
using CacheType = std::optional<
    std::pair<std::string, std::chrono::system_clock::time_point>>;

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

// Boundary-aware cache tests

TEST_CASE("Cache returns just now for 59s with expiration at 60s") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(59);
  CacheType cache;
  auto result = FormatRelativeTime(tp, cache);
  REQUIRE(result == "just now");
  REQUIRE(cache.has_value());
  const auto& [label, expiration] =
      *cache;  // NOLINT(bugprone-unchecked-optional-access)
  REQUIRE(label == "just now");
  // Expiration should be tp + 60s
  auto expected_exp = tp + std::chrono::seconds(60);
  REQUIRE(expiration == expected_exp);
}

TEST_CASE("Cache returns 1m ago for 61s with expiration at 120s") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(61);
  CacheType cache;
  auto result = FormatRelativeTime(tp, cache);
  REQUIRE(result == "1m ago");
  REQUIRE(cache.has_value());
  const auto& [label, expiration] =
      *cache;  // NOLINT(bugprone-unchecked-optional-access)
  REQUIRE(label == "1m ago");
  // diff=61, diff/60=1, (1+1)*60=120
  auto expected_exp = tp + std::chrono::seconds(120);
  REQUIRE(expiration == expected_exp);
}

TEST_CASE("Cache returns 5m ago for 350s with expiration at 360s") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(350);
  CacheType cache;
  auto result = FormatRelativeTime(tp, cache);
  REQUIRE(result == "5m ago");
  REQUIRE(cache.has_value());
  const auto& [label, expiration] =
      *cache;  // NOLINT(bugprone-unchecked-optional-access)
  REQUIRE(label == "5m ago");
  // diff=350, diff/60=5, (5+1)*60=360
  auto expected_exp = tp + std::chrono::seconds(360);
  REQUIRE(expiration == expected_exp);
}

TEST_CASE("Cache returns 1h ago for 3700s with expiration at 7200s") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(3700);
  CacheType cache;
  auto result = FormatRelativeTime(tp, cache);
  REQUIRE(result == "1h ago");
  REQUIRE(cache.has_value());
  const auto& [label, expiration] =
      *cache;  // NOLINT(bugprone-unchecked-optional-access)
  REQUIRE(label == "1h ago");
  // diff=3700, diff/3600=1, (1+1)*3600=7200
  auto expected_exp = tp + std::chrono::seconds(7200);
  REQUIRE(expiration == expected_exp);
}

TEST_CASE("Cache returns 1d ago for 90000s with expiration at 172800s") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(90000);
  CacheType cache;
  auto result = FormatRelativeTime(tp, cache);
  REQUIRE(result == "1d ago");
  REQUIRE(cache.has_value());
  const auto& [label, expiration] =
      *cache;  // NOLINT(bugprone-unchecked-optional-access)
  REQUIRE(label == "1d ago");
  // diff=90000, diff/86400=1, (1+1)*86400=172800
  auto expected_exp = tp + std::chrono::seconds(172800);
  REQUIRE(expiration == expected_exp);
}

TEST_CASE("Cache hit returns cached string before expiration") {
  auto now = std::chrono::system_clock::now();
  auto tp = now - std::chrono::seconds(30);
  // Pre-populate cache with a future expiration
  CacheType cache = std::make_pair(std::string("cached value"),
                                   now + std::chrono::seconds(60));
  auto result = FormatRelativeTime(tp, cache);
  REQUIRE(result == "cached value");
}

TEST_CASE("Cache miss recomputes when expired") {
  auto now = std::chrono::system_clock::now();
  auto tp = now - std::chrono::seconds(30);
  // Pre-populate cache with a past expiration
  CacheType cache = std::make_pair(std::string("stale cached"),
                                   now - std::chrono::seconds(10));
  auto result = FormatRelativeTime(tp, cache);
  REQUIRE(result == "just now");
  REQUIRE(cache.has_value());
  REQUIRE(cache->first == "just now");
}

TEST_CASE("Empty cache triggers fresh computation") {
  auto tp = std::chrono::system_clock::now() - std::chrono::seconds(90);
  CacheType cache;  // empty
  auto result = FormatRelativeTime(tp, cache);
  REQUIRE(result == "1m ago");
  REQUIRE(cache.has_value());
  const auto& [label, expiration] =
      *cache;  // NOLINT(bugprone-unchecked-optional-access)
  REQUIRE(label == "1m ago");
}
