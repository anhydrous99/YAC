#include "tool_call/workspace_filesystem.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace yac::tool_call {

namespace {

constexpr size_t kMaxContentPreviewBytes = 4000;

}  // namespace

WorkspaceFilesystem::WorkspaceFilesystem(std::filesystem::path workspace_root)
    : workspace_root_(std::filesystem::absolute(std::move(workspace_root))
                          .lexically_normal()) {}

std::filesystem::path WorkspaceFilesystem::ResolvePath(
    const std::string& path) const {
  std::filesystem::path candidate(path);
  if (candidate.is_relative()) {
    candidate = workspace_root_ / candidate;
  }
  candidate = std::filesystem::absolute(candidate).lexically_normal();

  const auto root = workspace_root_.string();
  const auto value = candidate.string();
  const auto with_separator =
      root.back() == std::filesystem::path::preferred_separator
          ? root
          : root + std::filesystem::path::preferred_separator;
  if (value != root && !value.starts_with(with_separator)) {
    throw std::runtime_error("Path is outside the workspace: " + path);
  }
  return candidate;
}

std::string WorkspaceFilesystem::DisplayPath(
    const std::filesystem::path& path) const {
  auto normalized = std::filesystem::absolute(path).lexically_normal();
  std::error_code error;
  auto relative = std::filesystem::relative(normalized, workspace_root_, error);
  if (!error && !relative.empty() && !relative.native().starts_with("..")) {
    return relative.string();
  }
  return normalized.string();
}

std::string WorkspaceFilesystem::ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    return {};
  }
  std::ostringstream buffer;
  buffer << file.rdbuf();
  return buffer.str();
}

void WorkspaceFilesystem::WriteFile(const std::filesystem::path& path,
                                    const std::string& content) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file) {
    throw std::runtime_error("Unable to open file for writing: " +
                             path.string());
  }
  file << content;
}

int CountLines(const std::string& text) {
  if (text.empty()) {
    return 0;
  }
  return static_cast<int>(std::count(text.begin(), text.end(), '\n')) +
         (text.back() == '\n' ? 0 : 1);
}

std::string PreviewText(const std::string& text) {
  if (text.size() <= kMaxContentPreviewBytes) {
    return text;
  }
  return text.substr(0, kMaxContentPreviewBytes) + "\n...";
}

size_t OffsetForLineCharacter(const std::string& text, int line,
                              int character) {
  const int target_line = std::max(1, line);
  const int target_character = std::max(1, character);
  size_t offset = 0;
  for (int current_line = 1; current_line < target_line && offset < text.size();
       ++current_line) {
    auto newline = text.find('\n', offset);
    if (newline == std::string::npos) {
      return text.size();
    }
    offset = newline + 1;
  }
  return std::min(text.size(),
                  offset + static_cast<size_t>(target_character - 1));
}

}  // namespace yac::tool_call
