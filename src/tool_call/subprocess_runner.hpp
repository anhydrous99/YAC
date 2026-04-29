#pragma once

#include "tool_call/process_limits.hpp"

#include <cstddef>
#include <filesystem>
#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace yac::tool_call {

struct SubprocessOptions {
  // Argument vector. argv[0] is passed to execvp. Caller need not append a
  // trailing nullptr; the runner does not require it.
  std::vector<const char*> argv;
  std::filesystem::path cwd;      // empty -> no chdir
  std::optional<int> timeout_ms;  // nullopt -> only stop_token bounds runtime
  size_t max_output_bytes = kMaxToolOutputBytes;
  int kill_grace_ms = kSubprocessKillGraceMs;
};

struct SubprocessResult {
  std::string output;
  int exit_code = -1;  // -1 if signaled / spawn failure / killed
  bool truncated = false;
  bool timed_out = false;
  bool cancelled = false;
  bool spawn_failed = false;  // pipe()/fork() failed
  std::string spawn_error;    // human-readable reason if spawn_failed
};

[[nodiscard]] SubprocessResult RunSubprocessCapture(
    const SubprocessOptions& opts, std::stop_token stop_token);

}  // namespace yac::tool_call
