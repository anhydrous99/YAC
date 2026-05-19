#include "tool_call/grep_tool_executor.hpp"

#include "tool_call/executor_arguments.hpp"
#include "tool_call/subprocess_runner.hpp"
#include "tool_call/tool_error_result.hpp"

#include <optional>
#include <sstream>
#include <string>
#include <unordered_set>
#include <vector>

namespace yac::tool_call {

namespace {

constexpr int kMaxMatches = 100;

struct GrepArgs {
  std::string pattern;
  std::string path;
  std::string include;
  bool case_sensitive = false;
  bool include_ignored = false;
};

struct ParsedGrepOutput {
  std::vector<GrepMatch> matches;
  int file_count = 0;
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
  return ErrorResult(
      GrepCall{.pattern = pattern}, error_msg,
      Json{{"pattern", pattern}, {"error", error_msg}, {"is_error", true}});
}

std::optional<GrepMatch> ParseGrepMatchLine(const std::string& line) {
  if (line.empty()) {
    return std::nullopt;
  }
  const auto parsed = Json::parse(line, nullptr, false);
  if (parsed.is_discarded() || !parsed.contains("type") ||
      parsed["type"] != "match") {
    return std::nullopt;
  }
  const auto& data = parsed["data"];
  if (!data.contains("path") || !data.contains("line_number") ||
      !data.contains("lines")) {
    return std::nullopt;
  }

  std::string content = data["lines"].value("text", std::string{});
  if (!content.empty() && content.back() == '\n') {
    content.pop_back();
  }
  return GrepMatch{.filepath = data["path"].value("text", std::string{}),
                   .line = data["line_number"].get<int>(),
                   .content = std::move(content)};
}

ParsedGrepOutput ParseGrepOutput(const std::string& output) {
  ParsedGrepOutput parsed;
  std::unordered_set<std::string> seen_files;
  std::istringstream stream(output);
  std::string line;
  while (std::getline(stream, line)) {
    auto match = ParseGrepMatchLine(line);
    if (!match.has_value()) {
      continue;
    }
    seen_files.insert(match->filepath);
    if (parsed.matches.size() < static_cast<size_t>(kMaxMatches)) {
      parsed.matches.push_back(std::move(*match));
    }
  }
  parsed.file_count = static_cast<int>(seen_files.size());
  return parsed;
}

ToolExecutionResult BuildGrepResult(const std::string& pattern,
                                    const std::string& output, bool truncated) {
  auto parsed = ParseGrepOutput(output);
  const bool capped = parsed.matches.size() >= static_cast<size_t>(kMaxMatches);
  const bool is_truncated = truncated || capped;

  Json matches_json = Json::array();
  for (const auto& m : parsed.matches) {
    matches_json.push_back(Json{
        {"filepath", m.filepath}, {"line", m.line}, {"content", m.content}});
  }

  const int match_count = static_cast<int>(parsed.matches.size());
  auto block = GrepCall{.pattern = pattern,
                        .match_count = match_count,
                        .matches = std::move(parsed.matches)};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = Json{
                                 {"pattern", pattern},
                                 {"match_count", match_count},
                                 {"file_count", parsed.file_count},
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

  SubprocessOptions opts{
      .argv = std::move(argv),
      .cwd = workspace_filesystem.Root(),
  };
  const auto run = RunSubprocessCapture(opts, stop_token);

  if (run.spawn_failed) {
    return MakeErrorResult(grep_args.pattern, run.spawn_error);
  }
  if (run.cancelled) {
    return MakeErrorResult(grep_args.pattern, "Cancelled");
  }
  if (run.exit_code == 127) {
    return MakeErrorResult(
        grep_args.pattern,
        "ripgrep (rg) not found in PATH. Install: 'apt install ripgrep' or "
        "'brew install ripgrep'.");
  }
  if (run.exit_code != 0 && run.exit_code != 1) {
    const std::string err_msg =
        run.output.empty()
            ? "rg exited with code " + std::to_string(run.exit_code)
            : run.output;
    return MakeErrorResult(grep_args.pattern, err_msg);
  }
  if (run.exit_code == 1) {
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

  return BuildGrepResult(grep_args.pattern, run.output, run.truncated);
}

}  // namespace yac::tool_call
