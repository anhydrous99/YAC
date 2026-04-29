#include "tool_call/bash_tool_executor.hpp"

#include "tool_call/executor_arguments.hpp"
#include "tool_call/subprocess_runner.hpp"
#include "tool_call/tool_error_result.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace yac::tool_call {

namespace {

constexpr int kDefaultTimeoutMs = 30000;
constexpr int kMaxTimeoutMs = 300000;
constexpr std::string_view kTruncationMarker = "\n[output truncated at 16KB]";

}  // namespace

ToolExecutionResult ExecuteBashTool(const chat::ToolCallRequest& request,
                                    const std::filesystem::path& workspace_root,
                                    std::stop_token stop_token) {
  const auto args = ParseArguments(request);
  const auto command = RequireString(args, "command");

  int timeout_ms = kDefaultTimeoutMs;
  if (args.contains("timeout_ms") && args["timeout_ms"].is_number_integer()) {
    timeout_ms = std::clamp(args["timeout_ms"].get<int>(), 100, kMaxTimeoutMs);
  }

  SubprocessOptions opts{
      .argv = {"/bin/sh", "-c", command.c_str(), nullptr},
      .cwd = workspace_root,
      .timeout_ms = timeout_ms,
  };
  auto run = RunSubprocessCapture(opts, stop_token);

  if (run.spawn_failed) {
    return ErrorResult(
        BashCall{
            .command = command, .output = run.spawn_error, .exit_code = -1},
        run.spawn_error, Json{{"exit_code", -1}, {"output", run.spawn_error}});
  }

  if (run.cancelled || run.timed_out) {
    const std::string reason =
        run.cancelled ? "Cancelled"
                      : "Timeout after " + std::to_string(timeout_ms) + "ms";
    return ErrorResult(
        BashCall{.command = command, .output = reason, .exit_code = -1}, reason,
        Json{{"exit_code", -1}, {"output", reason}, {"truncated", false}});
  }

  if (run.truncated) {
    run.output.append(kTruncationMarker);
  }
  const bool is_error = (run.exit_code != 0);
  auto block = BashCall{.command = command,
                        .output = run.output,
                        .exit_code = run.exit_code,
                        .is_error = is_error};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = Json{{"exit_code", run.exit_code},
                                                 {"output", run.output},
                                                 {"truncated", run.truncated}}
                                                .dump(),
                             .is_error = is_error};
}

}  // namespace yac::tool_call
