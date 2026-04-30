#include "tool_call/subprocess_runner.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <poll.h>
#include <sys/wait.h>
#include <unistd.h>

namespace yac::tool_call {

namespace {

void AppendCapped(std::string& output, const char* data, size_t n,
                  size_t max_output_bytes, bool& truncated) {
  if (output.size() >= max_output_bytes) {
    truncated = true;
    return;
  }
  const size_t to_add = std::min(n, max_output_bytes - output.size());
  output.append(data, to_add);
  if (output.size() >= max_output_bytes) {
    truncated = true;
  }
}

void KillWithGrace(pid_t pid, int kill_grace_ms) {
  kill(pid, SIGTERM);
  const auto grace_start = std::chrono::steady_clock::now();
  int status = 0;
  while (waitpid(pid, &status, WNOHANG) == 0) {
    const auto grace_elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - grace_start)
            .count();
    if (grace_elapsed >= kill_grace_ms) {
      kill(pid, SIGKILL);
      waitpid(pid, &status, 0);
      break;
    }
    usleep(10000);
  }
}

}  // namespace

SubprocessResult RunSubprocessCapture(const SubprocessOptions& opts,
                                      std::stop_token stop_token) {
  SubprocessResult result;

  std::array<int, 2> pipe_fds{};
  if (pipe(pipe_fds.data()) != 0) {
    result.spawn_failed = true;
    result.spawn_error = "pipe() failed";
    return result;
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    result.spawn_failed = true;
    result.spawn_error = "fork() failed";
    return result;
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

    if (!opts.cwd.empty()) {
      chdir(opts.cwd.c_str());
    }

    // execvp's POSIX signature takes char* const argv[]; the const_cast is
    // required to bridge from our std::vector<const char*> argument storage.
    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-const-cast)
    execvp(opts.argv[0], const_cast<char* const*>(opts.argv.data()));
    _exit(127);
  }

  close(pipe_fds[1]);

  const int read_fd = pipe_fds[0];
  fcntl(read_fd, F_SETFL, fcntl(read_fd, F_GETFL) | O_NONBLOCK);

  result.output.reserve(4096);
  std::array<char, 4096> buf{};
  const auto start = std::chrono::steady_clock::now();
  bool child_reaped = false;
  int captured_status = 0;

  while (true) {
    if (stop_token.stop_requested()) {
      result.cancelled = true;
      break;
    }

    int poll_ms = 50;
    if (opts.timeout_ms.has_value()) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - start)
              .count();
      if (elapsed >= *opts.timeout_ms) {
        result.timed_out = true;
        break;
      }
      const int remaining_ms =
          static_cast<int>(*opts.timeout_ms - static_cast<int>(elapsed));
      poll_ms = std::min(remaining_ms, 50);
    }

    struct pollfd pfd{};
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
          AppendCapped(result.output, buf.data(), static_cast<size_t>(n),
                       opts.max_output_bytes, result.truncated);
          if (result.truncated) {
            break;
          }
        }
        captured_status = status;
        child_reaped = true;
        break;
      }
      continue;
    }

    if ((pfd.revents & POLLIN) != 0) {
      const ssize_t n = read(read_fd, buf.data(), buf.size());
      if (n > 0) {
        AppendCapped(result.output, buf.data(), static_cast<size_t>(n),
                     opts.max_output_bytes, result.truncated);
      } else if (n == 0 || (n < 0 && errno != EAGAIN && errno != EINTR)) {
        break;
      }
    }

    if ((pfd.revents & (POLLHUP | POLLERR)) != 0) {
      ssize_t n = 0;
      while ((n = read(read_fd, buf.data(), buf.size())) > 0) {
        AppendCapped(result.output, buf.data(), static_cast<size_t>(n),
                     opts.max_output_bytes, result.truncated);
      }
      break;
    }
  }

  close(read_fd);

  if (result.cancelled || result.timed_out) {
    KillWithGrace(pid, opts.kill_grace_ms);
    return result;
  }

  if (!child_reaped) {
    waitpid(pid, &captured_status, 0);
  }
  result.exit_code =
      WIFEXITED(captured_status) ? WEXITSTATUS(captured_status) : -1;
  return result;
}

}  // namespace yac::tool_call
