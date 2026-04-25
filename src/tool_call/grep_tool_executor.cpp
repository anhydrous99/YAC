#include "tool_call/grep_tool_executor.hpp"

#include "tool_call/executor_arguments.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sstream>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

namespace yac::tool_call {

namespace {

constexpr size_t kMaxOutputBytes = 16384;
constexpr int kMaxMatches = 100;
constexpr int kKillGraceMs = 2000;

struct GrepArgs {
  std::string pattern;
  std::string path;
  std::string include;
  bool case_sensitive = false;
  bool include_ignored = false;
};

GrepArgs ParseGrepArgs(const chat::ToolCallRequest& request) {
  const auto args = ParseArguments(request);
  GrepArgs result;
  result.pattern = RequireString(args, "pattern");
  result.path = OptionalString(args, "path");
  result.include = OptionalString(args, "include");
  result.case_sensitive = OptionalBool(args, "case_sensitive", false);
  result.include_ignored = OptionalBool(args, "include_ignored", false);
  return result;
}

ToolExecutionResult MakeErrorResult(const std::string& pattern,
                                    const std::string& error_msg) {
  auto block =
      GrepCall{.pattern = pattern, .is_error = true, .error = error_msg};
  return ToolExecutionResult{
      .block = std::move(block),
      .result_json =
          Json{{"pattern", pattern}, {"error", error_msg}, {"is_error", true}}
              .dump(),
      .is_error = true};
}

ToolExecutionResult BuildGrepResult(const std::string& pattern,
                                    const std::string& output, bool truncated) {
  std::vector<GrepMatch> matches;
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line) &&
         matches.size() < static_cast<size_t>(kMaxMatches)) {
    if (line.empty()) {
      continue;
    }
    const auto parsed = Json::parse(line, nullptr, false);
    if (parsed.is_discarded()) {
      continue;
    }
    if (!parsed.contains("type") || parsed["type"] != "match") {
      continue;
    }
    const auto& data = parsed["data"];
    if (!data.contains("path") || !data.contains("line_number") ||
        !data.contains("lines")) {
      continue;
    }
    std::string filepath = data["path"].value("text", std::string{});
    const int line_number = data["line_number"].get<int>();
    std::string content = data["lines"].value("text", std::string{});
    if (!content.empty() && content.back() == '\n') {
      content.pop_back();
    }
    matches.push_back(GrepMatch{
        .filepath = filepath, .line = line_number, .content = content});
  }

  const bool capped = matches.size() >= static_cast<size_t>(kMaxMatches);
  const bool is_truncated = truncated || capped;

  int file_count = 0;
  {
    std::istringstream fs(output);
    std::string fl;
    std::vector<std::string> seen_files;
    while (std::getline(fs, fl)) {
      if (fl.empty()) {
        continue;
      }
      const auto p = Json::parse(fl, nullptr, false);
      if (p.is_discarded() || !p.contains("type") || p["type"] != "match") {
        continue;
      }
      std::string fp = p["data"]["path"].value("text", std::string{});
      if (std::find(seen_files.begin(), seen_files.end(), fp) ==
          seen_files.end()) {
        seen_files.push_back(fp);
        file_count++;
      }
    }
  }

  Json matches_json = Json::array();
  for (const auto& m : matches) {
    matches_json.push_back(Json{
        {"filepath", m.filepath}, {"line", m.line}, {"content", m.content}});
  }

  const int match_count = static_cast<int>(matches.size());
  auto block = GrepCall{.pattern = pattern,
                        .match_count = match_count,
                        .matches = std::move(matches)};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = Json{
                                 {"pattern", pattern},
                                 {"match_count", match_count},
                                 {"file_count", file_count},
                                 {"matches", matches_json},
                                 {"truncated",
                                  is_truncated}}.dump()};
}

}  // namespace

ToolExecutionResult ExecuteGrepTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem,
    std::stop_token stop_token) {
  GrepArgs grep_args;
  try {
    grep_args = ParseGrepArgs(request);
  } catch (const std::exception& e) {
    return MakeErrorResult("", e.what());
  }

  const std::filesystem::path search_path =
      grep_args.path.empty() ? workspace_filesystem.Root()
                             : workspace_filesystem.ResolvePath(grep_args.path);

  std::vector<const char*> argv;
  argv.push_back("rg");
  argv.push_back("--json");
  if (grep_args.include_ignored) {
    argv.push_back("--no-ignore");
  }
  if (!grep_args.case_sensitive) {
    argv.push_back("-i");
  }

  std::string include_val;
  if (!grep_args.include.empty()) {
    include_val = grep_args.include;
    argv.push_back("-g");
    argv.push_back(include_val.c_str());
  }

  argv.push_back("-e");
  argv.push_back(grep_args.pattern.c_str());

  const std::string path_str = search_path.string();
  argv.push_back(path_str.c_str());
  argv.push_back(nullptr);

  int pipe_fds[2];
  if (pipe(pipe_fds) != 0) {
    return MakeErrorResult(grep_args.pattern, "pipe() failed");
  }

  const pid_t pid = fork();
  if (pid < 0) {
    close(pipe_fds[0]);
    close(pipe_fds[1]);
    return MakeErrorResult(grep_args.pattern, "fork() failed");
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

    const std::filesystem::path& root = workspace_filesystem.Root();
    if (!root.empty()) {
      chdir(root.c_str());
    }

    execvp("rg", const_cast<char* const*>(argv.data()));
    _exit(127);
  }

  close(pipe_fds[1]);

  const int read_fd = pipe_fds[0];
  fcntl(read_fd, F_SETFL, fcntl(read_fd, F_GETFL) | O_NONBLOCK);

  std::string output;
  output.reserve(4096);
  bool truncated = false;

  std::array<char, 4096> buf{};

  while (true) {
    if (stop_token.stop_requested()) {
      break;
    }

    struct pollfd pfd {};
    pfd.fd = read_fd;
    pfd.events = POLLIN;
    const int ready = poll(&pfd, 1, 50);

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

        const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

        if (exit_code == 127) {
          return MakeErrorResult(
              grep_args.pattern,
              "ripgrep (rg) not found in PATH. Install: 'apt install ripgrep' "
              "or 'brew install ripgrep'.");
        }

        if (exit_code != 0 && exit_code != 1) {
          const std::string err_msg =
              output.empty()
                  ? "rg exited with code " + std::to_string(exit_code)
                  : output;
          return MakeErrorResult(grep_args.pattern, err_msg);
        }

        if (exit_code == 1) {
          auto block = GrepCall{.pattern = grep_args.pattern, .match_count = 0};
          return ToolExecutionResult{.block = std::move(block),
                                     .result_json = Json{
                                         {"pattern", grep_args.pattern},
                                         {"match_count", 0},
                                         {"file_count", 0},
                                         {"matches", Json::array()},
                                         {"truncated",
                                          false}}.dump()};
        }

        return BuildGrepResult(grep_args.pattern, output, truncated);
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

  if (stop_token.stop_requested()) {
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
    return MakeErrorResult(grep_args.pattern, "Cancelled");
  }

  int status = 0;
  waitpid(pid, &status, 0);
  const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;

  if (exit_code == 127) {
    return MakeErrorResult(
        grep_args.pattern,
        "ripgrep (rg) not found in PATH. Install: 'apt install ripgrep' or "
        "'brew install ripgrep'.");
  }

  if (exit_code != 0 && exit_code != 1) {
    const std::string err_msg =
        output.empty() ? "rg exited with code " + std::to_string(exit_code)
                       : output;
    return MakeErrorResult(grep_args.pattern, err_msg);
  }

  if (exit_code == 1) {
    auto block = GrepCall{.pattern = grep_args.pattern, .match_count = 0};
    return ToolExecutionResult{.block = std::move(block),
                               .result_json = Json{
                                   {"pattern", grep_args.pattern},
                                   {"match_count", 0},
                                   {"file_count", 0},
                                   {"matches", Json::array()},
                                   {"truncated",
                                    false}}.dump()};
  }

  return BuildGrepResult(grep_args.pattern, output, truncated);
}

}  // namespace yac::tool_call
