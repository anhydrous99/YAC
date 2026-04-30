#include "tool_call/glob_tool_executor.hpp"

#include "core_types/tool_call_types.hpp"
#include "tool_call/executor_arguments.hpp"
#include "tool_call/gitignore_filter.hpp"
#include "tool_call/glob_matcher.hpp"

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace yac::tool_call {

namespace {

constexpr size_t kMaxResults = 200;

}

ToolExecutionResult ExecuteGlobTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  const auto pattern = RequireString(args, "pattern");
  const auto path_arg = OptionalString(args, "path");
  const bool include_ignored = OptionalBool(args, "include_ignored", false);

  const std::filesystem::path walk_root =
      path_arg.empty() ? workspace_filesystem.Root()
                       : workspace_filesystem.ResolvePath(path_arg);

  std::optional<GitignoreFilter> filter;
  if (!include_ignored) {
    filter.emplace(workspace_filesystem.Root());
  }

  const CompiledGlob compiled(pattern);

  using FileEntry =
      std::pair<std::filesystem::path, std::filesystem::file_time_type>;
  std::vector<FileEntry> matches;

  std::filesystem::recursive_directory_iterator it(
      walk_root, std::filesystem::directory_options::skip_permission_denied);
  for (const auto& entry : it) {
    if (!entry.is_regular_file()) {
      continue;
    }
    const std::string relative =
        std::filesystem::relative(entry.path(), walk_root).string();
    if (filter && filter->ShouldSkip(relative)) {
      continue;
    }
    if (!compiled.Match(relative)) {
      continue;
    }
    matches.emplace_back(entry.path(), entry.last_write_time());
  }

  std::ranges::sort(matches, [](const FileEntry& lhs, const FileEntry& rhs) {
    return lhs.second > rhs.second;
  });

  const bool truncated = matches.size() > kMaxResults;
  if (truncated) {
    matches.resize(kMaxResults);
  }

  std::vector<std::string> matched_files;
  matched_files.reserve(matches.size());
  for (const auto& [path, _] : matches) {
    matched_files.push_back(workspace_filesystem.DisplayPath(path));
  }

  auto block = GlobCall{.pattern = pattern, .matched_files = matched_files};

  Json result{{"pattern", pattern},
              {"match_count", static_cast<int>(matched_files.size())},
              {"matches", matched_files},
              {"truncated", truncated}};

  return ToolExecutionResult{.block = std::move(block),
                             .result_json = result.dump()};
}

}  // namespace yac::tool_call
