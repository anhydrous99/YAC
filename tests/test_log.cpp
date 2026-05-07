#include "util/log.hpp"

#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <catch2/catch_test_macros.hpp>

namespace {

struct CapturedRecord {
  yac::log::Level level;
  std::string module;
  std::string message;
};

class CapturingSink final : public yac::log::Sink {
 public:
  void Write(yac::log::Level level, std::string_view module,
             std::string_view message) override {
    std::scoped_lock lock(mutex_);
    records_.push_back({.level = level,
                        .module = std::string(module),
                        .message = std::string(message)});
  }

  std::vector<CapturedRecord> Records() {
    std::scoped_lock lock(mutex_);
    return records_;
  }

 private:
  std::mutex mutex_;
  std::vector<CapturedRecord> records_;
};

class SinkResetGuard {
 public:
  SinkResetGuard() = default;
  SinkResetGuard(const SinkResetGuard&) = delete;
  SinkResetGuard& operator=(const SinkResetGuard&) = delete;
  SinkResetGuard(SinkResetGuard&&) = delete;
  SinkResetGuard& operator=(SinkResetGuard&&) = delete;
  ~SinkResetGuard() { yac::log::SetSink(nullptr); }
};

}  // namespace

TEST_CASE("default sink writes a level/module/message line to stderr",
          "[log]") {
  SinkResetGuard guard;
  yac::log::SetSink(nullptr);

  std::stringstream captured;
  auto* old_buf = std::cerr.rdbuf(captured.rdbuf());
  yac::log::Info("logtest", "hello {}", "world");
  std::cerr.rdbuf(old_buf);

  const std::string text = captured.str();
  REQUIRE(text.find("[INFO]") != std::string::npos);
  REQUIRE(text.find("[logtest]") != std::string::npos);
  REQUIRE(text.find("hello world") != std::string::npos);
  REQUIRE(!text.empty());
  REQUIRE(text.back() == '\n');
}

TEST_CASE("each level produces output with correct level value", "[log]") {
  SinkResetGuard guard;
  auto sink = std::make_shared<CapturingSink>();
  yac::log::SetSink(sink);

  yac::log::Debug("mod", "a");
  yac::log::Info("mod", "b");
  yac::log::Warn("mod", "c");
  yac::log::Error("mod", "d");

  const auto records = sink->Records();
  REQUIRE(records.size() == 4);
  REQUIRE(records[0].level == yac::log::Level::Debug);
  REQUIRE(records[1].level == yac::log::Level::Info);
  REQUIRE(records[2].level == yac::log::Level::Warn);
  REQUIRE(records[3].level == yac::log::Level::Error);
  REQUIRE(records[0].module == "mod");
  REQUIRE(records[1].message == "b");
}

TEST_CASE("SetSink redirects output to the supplied sink", "[log]") {
  SinkResetGuard guard;
  auto sink = std::make_shared<CapturingSink>();
  yac::log::SetSink(sink);
  REQUIRE(yac::log::GetSink().get() == sink.get());

  yac::log::Info("router", "value={}", 42);

  const auto records = sink->Records();
  REQUIRE(records.size() == 1);
  REQUIRE(records[0].module == "router");
  REQUIRE(records[0].message == "value=42");
}

TEST_CASE("Redact masks api_key=, Bearer, code=, and token= values", "[log]") {
  REQUIRE(yac::log::Redact("api_key=secret") == "api_key=***REDACTED***");
  REQUIRE(yac::log::Redact("Bearer sk-abc123") == "Bearer ***REDACTED***");
  REQUIRE(yac::log::Redact("https://x/?code=oauth-abc&state=ok") ==
          "https://x/?code=***REDACTED***&state=ok");
  REQUIRE(yac::log::Redact("token=hunter2&user=me") ==
          "token=***REDACTED***&user=me");
}

TEST_CASE("Redact preserves surrounding URL structure", "[log]") {
  const std::string in =
      "https://api.example.com/v1?api_key=AKIAIOSFODNN7EXAMPLE&format=json";
  const std::string out = yac::log::Redact(in);
  REQUIRE(out.find("***REDACTED***") != std::string::npos);
  REQUIRE(out.find("AKIAIOSFODNN7EXAMPLE") == std::string::npos);
  REQUIRE(out.find("format=json") != std::string::npos);
}

TEST_CASE("Redact does not mask non-secrets", "[log]") {
  REQUIRE(yac::log::Redact("hello world") == "hello world");
  REQUIRE(yac::log::Redact("the user submitted code review") ==
          "the user submitted code review");
  REQUIRE(yac::log::Redact("tokenize this string") == "tokenize this string");
  REQUIRE(yac::log::Redact("") == "");
}

TEST_CASE("DescribeCurrentException returns the active exception's message",
          "[log]") {
  std::string described;
  try {
    throw std::runtime_error("kaboom");
  } catch (...) {
    described = yac::log::DescribeCurrentException();
  }
  REQUIRE(described == "kaboom");
}

TEST_CASE("DescribeCurrentException reports unknown exceptions", "[log]") {
  std::string described;
  try {
    throw 42;
  } catch (...) {
    described = yac::log::DescribeCurrentException();
  }
  REQUIRE(described == "unknown exception");
}
