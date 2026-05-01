#include "tool_call/filesystem_tool_executor.hpp"

#include "tool_call/executor_arguments.hpp"

#include <algorithm>

namespace yac::tool_call {

namespace {

constexpr size_t kMaxListDirEntries = 200;

std::string DirectoryEntryTypeToJson(DirectoryEntryType type) {
  switch (type) {
    case DirectoryEntryType::File:
      return "file";
    case DirectoryEntryType::Directory:
      return "dir";
    case DirectoryEntryType::Other:
      return "other";
  }
  return "other";
}

}  // namespace

ToolExecutionResult ExecuteFileReadTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  const auto filepath = RequireString(args, "filepath");
  const auto path = workspace_filesystem.ResolvePath(filepath);
  if (!std::filesystem::exists(path)) {
    throw std::runtime_error("file not found: " + filepath);
  }
  const auto content = WorkspaceFilesystem::ReadFile(path);
  const auto lines_loaded = CountLines(content);

  auto block = FileReadCall{.filepath = workspace_filesystem.DisplayPath(path),
                            .lines_loaded = lines_loaded,
                            .excerpt = PreviewText(content)};
  Json result{{"filepath", block.filepath},
              {"lines_loaded", lines_loaded},
              {"content", content}};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = result.dump()};
}

ToolExecutionResult ExecuteFileWriteTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  const auto filepath = RequireString(args, "filepath");
  const auto content = RequireString(args, "content");
  const auto path = workspace_filesystem.ResolvePath(filepath);
  const auto old_content = WorkspaceFilesystem::ReadFile(path);
  const auto lines_removed = CountLines(old_content);
  const auto lines_added = CountLines(content);
  WorkspaceFilesystem::WriteFile(path, content);

  auto block = FileWriteCall{.filepath = workspace_filesystem.DisplayPath(path),
                             .content_preview = PreviewText(content),
                             .content_tail = TailLines(content, 3),
                             .lines_added = lines_added,
                             .lines_removed = lines_removed};
  Json result{{"filepath", block.filepath},
              {"lines_added", lines_added},
              {"lines_removed", lines_removed}};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = result.dump()};
}

ToolExecutionResult ExecuteListDirTool(
    const chat::ToolCallRequest& request,
    const WorkspaceFilesystem& workspace_filesystem) {
  const auto args = ParseArguments(request);
  const auto requested_path = RequireString(args, "path");
  const auto path = workspace_filesystem.ResolvePath(requested_path);
  if (!std::filesystem::is_directory(path)) {
    throw std::runtime_error("Path is not a directory: " + requested_path);
  }

  std::vector<DirectoryEntry> entries;
  for (const auto& entry : std::filesystem::directory_iterator(path)) {
    DirectoryEntryType type = DirectoryEntryType::Other;
    uintmax_t size = 0;
    if (entry.is_directory()) {
      type = DirectoryEntryType::Directory;
    } else if (entry.is_regular_file()) {
      type = DirectoryEntryType::File;
      size = entry.file_size();
    }
    entries.push_back(DirectoryEntry{
        .name = entry.path().filename().string(), .type = type, .size = size});
  }
  std::ranges::sort(entries,
                    [](const DirectoryEntry& lhs, const DirectoryEntry& rhs) {
                      if (lhs.type != rhs.type) {
                        return lhs.type == DirectoryEntryType::Directory;
                      }
                      return lhs.name < rhs.name;
                    });
  const bool truncated = entries.size() > kMaxListDirEntries;
  if (truncated) {
    entries.resize(kMaxListDirEntries);
  }

  Json result{{"path", workspace_filesystem.DisplayPath(path)},
              {"truncated", truncated},
              {"entries", Json::array()}};
  for (const auto& entry : entries) {
    result["entries"].push_back({{"name", entry.name},
                                 {"type", DirectoryEntryTypeToJson(entry.type)},
                                 {"size", entry.size}});
  }
  auto block = ListDirCall{.path = workspace_filesystem.DisplayPath(path),
                           .entries = std::move(entries),
                           .truncated = truncated};
  return ToolExecutionResult{.block = std::move(block),
                             .result_json = result.dump()};
}

}  // namespace yac::tool_call
