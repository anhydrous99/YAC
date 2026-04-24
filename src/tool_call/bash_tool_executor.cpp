#include "tool_call/bash_tool_executor.hpp"

#include "tool_call/executor_arguments.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <utility>

namespace yac::tool_call {

namespace {

constexpr int kDefaultTimeoutMs = 30000;
constexpr int kMaxTimeoutMs = 300000;
constexpr size_t kMaxOutputBytes = 16384;
constexpr int kKillGraceMs = 2000;
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

  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    auto block = BashCall{.command = command,
                          .output = "pipe() failed",
                          .exit_code = -1,
                          .is_error = true};
    return ToolExecutionResult{
        .block = std::move(block),
        .result_json =
            Json{{"exit_code", -1}, {"output", "pipe() failed"}}.dump(),
        .is_error = true};
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    auto block = BashCall{.command = command,
                          .output = "fork() failed",
                          .exit_code = -1,
                          .is_error = true};
    return ToolExecutionResult{
        .block = std::move(block),
        .result_json =
            Json{{"exit_code", -1}, {"output", "fork() failed"}}.dump(),
        .is_error = true};
  }

  if (pid == 0) {
    close(pipe_fds[0]);
    dup2(pipe_fds[1], STDOUT_FILENO);
    dup2(pipe_fds[1], STDERR_FILENO);
    close(pipe_fds[1]);

    const int dev_null = open("/dev/null", O_RDONLY);
    if (dev_null >= 0) {
      dup2(dev_null, STDIN_FILENO);
      close(dev_null);
    }

    if (!workspace_root.empty()) {
      chdir(workspace_root.c_str());
    }

    const char* argv[] = {"sh", "-c", command.c_str(), nullptr};
    execvp("/bin/sh", const_cast<char* const*>(argv));
    _exit(127);
  }

  close(pipe_fds[1]);

  const int read_fd = pipe_fds[0];
  fcntl(read_fd, F_SETFL, fcntl(read_fd, F_GETFL) | O_NONBLOCK);

  std::string output;
  output.reserve(4096);
  bool truncated = false;
  bool timed_out = false;
  bool cancelled = false;

  const auto start = std::chrono::steady_clock::now();
  std::array<char, 4096> buf{};

  while (true) {
    if (stop_token.stop_requested()) {
      cancelled = true;
      break;
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    if (elapsed >= timeout_ms) {
      timed_out = true;
      break;
    }

    const int remaining_ms =
        static_cast<int>(timeout_ms - static_cast<int>(elapsed));
    const int poll_ms = std::min(remaining_ms, 50);

    struct pollfd pfd {};
    pfd.fd = read_fd;
    pfd.events = POLLIN;
    const int ready = poll(&pfd, 1, poll_ms);

    if (ready < 0) {
      break;
    }
    if (ready == 0) {
      int status = 0;
      if (waitpid(pid, &status, WNOHANG) > 0) {
        ssize_t n = 0;
        while ((n = read(read_fd, buf.data(), buf.size())) > 0) {
          const size_t to_add =
              std::min(static_cast<size_t>(n), kMaxOutputBytes - output.size());
          output.append(buf.data(), to_add);
          if (output.size() >= kMaxOutputBytes) {
            truncated = true;
            break;
          }
        }
        close(read_fd);
        if (truncated) {
          output += kTruncationMarker;
        }
        const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
        const bool is_error = (exit_code != 0);
        auto block = BashCall{.command = command,
                              .output = output,
                              .exit_code = exit_code,
                              .is_error = is_error};
        return ToolExecutionResult{.block = std::move(block),
                                   .result_json = Json{{"exit_code", exit_code},
                                                       {"output", output},
                                                       {"truncated", truncated}}
                                                      .dump(),
                                   .is_error = is_error};
      }
      continue;
    }

    if ((pfd.revents & POLLIN) != 0) {
      const ssize_t n = read(read_fd, buf.data(), buf.size());
      if (n > 0) {
        if (output.size() < kMaxOutputBytes) {
          const size_t to_add =
              std::min(static_cast<size_t>(n), kMaxOutputBytes - output.size());
          output.append(buf.data(), to_add);
          if (output.size() >= kMaxOutputBytes) {
            truncated = true;
          }
        }
      } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
        break;
      }
    }

    if ((pfd.revents & (POLLHUP | POLLERR)) != 0) {
      ssize_t n = 0;
      while ((n = read(read_fd, buf.data(), buf.size())) > 0) {
        if (output.size() < kMaxOutputBytes) {
          const size_t to_add =
              std::min(static_cast<size_t>(n), kMaxOutputBytes - output.size());
          output.append(buf.data(), to_add);
          if (output.size() >= kMaxOutputBytes) {
            truncated = true;
          }
        }
      }
      break;
    }
  }

  close(read_fd);

  if (timed_out || cancelled) {
    kill(pid, SIGTERM);
    const auto grace_start = std::chrono::steady_clock::now();
    int status = 0;
    while (waitpid(pid, &status, WNOHANG) == 0) {
      const auto grace_elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - grace_start)
              .count();
      if (grace_elapsed >= kKillGraceMs) {
        kill(pid, SIGKILL);
        waitpid(pid, &status, 0);
        break;
      }
      usleep(10000);
    }
    const std::string reason =
        cancelled ? "Cancelled"
                  : "Timeout after " + std::to_string(timeout_ms) + "ms";
    auto block = BashCall{.command = command,
                          .output = reason,
                          .exit_code = -1,
                          .is_error = true};
    return ToolExecutionResult{
        .block = std::move(block),
        .result_json =
            Json{{"exit_code", -1}, {"output", reason}, {"truncated", false}}
                .dump(),
        .is_error = true};
  }

  int status = 0;
  waitpid(pid, &status, 0);

  if (truncated) {
    output += kTruncationMarker;
  }

  const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  const bool is_error = (exit_code != 0);
  auto block = BashCall{.command = command,
                        .output = output,
                        .exit_code = exit_code,
                        .is_error = is_error};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = Json{{"exit_code", exit_code},
                                                 {"output", output},
                                                 {"truncated", truncated}}
                                                .dump(),
                             .is_error = is_error};
}

}  // namespace yac::tool_call
