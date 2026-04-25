#include "tool_call/edit_tool_executor.hpp"

#include "tool_call/edit_replacers.hpp"
#include "tool_call/executor_arguments.hpp"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace yac::tool_call {

namespace {

bool ContainsBinaryBytes(const std::string& content) {
  const auto bytes_to_scan = std::min(content.size(), size_t{8192});
  const auto null_position = content.find('\0');
  return null_position != std::string::npos && null_position < bytes_to_scan;
}

}  // namespace

ToolExecutionResult ExecuteEditTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  const auto filepath = RequireString(args, "filepath");
  const auto old_string = RequireString(args, "old_string");
  const auto new_string = RequireString(args, "new_string");
  const bool replace_all = OptionalBool(args, "replace_all", false);

  if (old_string.empty()) {
    throw std::runtime_error(
        "old_string must not be empty; use file_write to create new files.");
  }
  if (old_string == new_string) {
    throw std::runtime_error(
        "old_string and new_string are identical; no change to apply.");
  }

  const auto path = workspace_filesystem.ResolvePath(filepath);
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error("file not found: " + filepath);
  }

  const auto old_content = WorkspaceFilesystem::ReadFile(path);
  if (ContainsBinaryBytes(old_content)) {
    throw std::runtime_error("Cannot edit binary file: " + filepath +
                             ". Use file_write for binary content.");
  }

  std::string new_content;
  if (replace_all) {
    new_content = ReplaceAll(old_content, old_string, new_string);
  } else if (const auto replaced =
                 SimpleReplacer(old_content, old_string, new_string);
             replaced.has_value()) {
    new_content = *replaced;
  } else if (const auto replaced =
                 LineTrimmedReplacer(old_content, old_string, new_string);
             replaced.has_value()) {
    new_content = *replaced;
  } else if (const auto replaced =
                 WhitespaceNormalizedReplacer(old_content, old_string,
                                             new_string);
             replaced.has_value()) {
    new_content = *replaced;
  } else {
    throw std::runtime_error(
        "Could not find old_string in file. Match must be exact "
        "(whitespace-tolerant fallbacks also failed).");
  }

  WorkspaceFilesystem::WriteFile(path, new_content);
  auto diff = ComputeDiff(old_content, new_content);

  size_t additions = 0;
  size_t deletions = 0;
  for (const auto& line : diff) {
    if (line.type == DiffLine::Add) {
      ++additions;
    } else if (line.type == DiffLine::Remove) {
      ++deletions;
    }
  }

  const auto display_path = workspace_filesystem.DisplayPath(path);
  auto block = FileEditCall{.filepath = display_path, .diff = std::move(diff)};
  Json result{{"filepath", display_path},
              {"diff_lines", block.diff.size()},
              {"additions", additions},
              {"deletions", deletions}};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = result.dump()};
}

}  // namespace yac::tool_call
