#include "tool_call/subprocess_runner.hpp"

#include <chrono>
#include <stop_token>
#include <thread>

#include <catch2/catch_test_macros.hpp>

namespace {

using yac::tool_call::RunSubprocessCapture;
using yac::tool_call::SubprocessOptions;

}  // namespace

TEST_CASE("SubprocessRunner: echo captures stdout") {
  SubprocessOptions opts{
      .argv = {"/bin/sh", "-c", "echo hello world", nullptr},
  };
  std::stop_source ss;
  const auto result = RunSubprocessCapture(opts, ss.get_token());

  REQUIRE_FALSE(result.spawn_failed);
  REQUIRE_FALSE(result.timed_out);
  REQUIRE_FALSE(result.cancelled);
  REQUIRE(result.exit_code == 0);
  REQUIRE(result.output.find("hello world") != std::string::npos);
}

TEST_CASE("SubprocessRunner: stderr is merged into output") {
  SubprocessOptions opts{
      .argv = {"/bin/sh", "-c", "echo to_stderr 1>&2", nullptr},
  };
  std::stop_source ss;
  const auto result = RunSubprocessCapture(opts, ss.get_token());

  REQUIRE(result.exit_code == 0);
  REQUIRE(result.output.find("to_stderr") != std::string::npos);
}

TEST_CASE("SubprocessRunner: non-zero exit code is reported") {
  SubprocessOptions opts{
      .argv = {"/bin/sh", "-c", "exit 42", nullptr},
  };
  std::stop_source ss;
  const auto result = RunSubprocessCapture(opts, ss.get_token());

  REQUIRE_FALSE(result.spawn_failed);
  REQUIRE(result.exit_code == 42);
}

TEST_CASE("SubprocessRunner: missing binary -> exit_code 127") {
  SubprocessOptions opts{
      .argv = {"this_binary_definitely_does_not_exist_xyzzy", nullptr},
  };
  std::stop_source ss;
  const auto result = RunSubprocessCapture(opts, ss.get_token());

  REQUIRE_FALSE(
      result.spawn_failed);  // pipe()/fork() succeeded; child _exit(127)
  REQUIRE(result.exit_code == 127);
}

TEST_CASE("SubprocessRunner: timeout kills runaway command") {
  SubprocessOptions opts{
      .argv = {"/bin/sh", "-c", "sleep 30", nullptr},
      .timeout_ms = 200,
  };
  std::stop_source ss;
  const auto start = std::chrono::steady_clock::now();
  const auto result = RunSubprocessCapture(opts, ss.get_token());
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();

  REQUIRE(result.timed_out);
  REQUIRE_FALSE(result.cancelled);
  REQUIRE(result.exit_code == -1);
  // Should return promptly after timeout + grace; well under sleep duration.
  REQUIRE(elapsed < 5000);
}

TEST_CASE("SubprocessRunner: stop_token cancels the run") {
  SubprocessOptions opts{
      .argv = {"/bin/sh", "-c", "sleep 30", nullptr},
  };
  std::stop_source ss;

  std::thread cancel_thread([&] {
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    ss.request_stop();
  });

  const auto start = std::chrono::steady_clock::now();
  const auto result = RunSubprocessCapture(opts, ss.get_token());
  const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                           std::chrono::steady_clock::now() - start)
                           .count();
  cancel_thread.join();

  REQUIRE(result.cancelled);
  REQUIRE_FALSE(result.timed_out);
  REQUIRE(result.exit_code == -1);
  REQUIRE(elapsed < 5000);
}

TEST_CASE("SubprocessRunner: output truncates at max_output_bytes") {
  // Print 32KB of 'x' chars.
  SubprocessOptions opts{
      .argv = {"/bin/sh", "-c", "yes x | head -c 32768 | tr -d '\\n'", nullptr},
      .max_output_bytes = 1024,
  };
  std::stop_source ss;
  const auto result = RunSubprocessCapture(opts, ss.get_token());

  REQUIRE(result.truncated);
  REQUIRE(result.output.size() <= 1024);
}

TEST_CASE("SubprocessRunner: cwd switches the process working directory") {
  SubprocessOptions opts{
      .argv = {"/bin/sh", "-c", "pwd", nullptr},
      .cwd = "/tmp",
  };
  std::stop_source ss;
  const auto result = RunSubprocessCapture(opts, ss.get_token());

  REQUIRE(result.exit_code == 0);
  REQUIRE(result.output.find("/tmp") != std::string::npos);
}
