#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace yac::tool_call {

class WorkspaceFilesystem {
 public:
  explicit WorkspaceFilesystem(std::filesystem::path workspace_root);

  [[nodiscard]] std::filesystem::path ResolvePath(
      const std::string& path) const;
  [[nodiscard]] std::string DisplayPath(
      const std::filesystem::path& path) const;
  [[nodiscard]] static std::string ReadFile(const std::filesystem::path& path);
  static void WriteFile(const std::filesystem::path& path,
                        const std::string& content);

 private:
  std::filesystem::path workspace_root_;
};

[[nodiscard]] int CountLines(const std::string& text);
[[nodiscard]] std::string PreviewText(const std::string& text);
[[nodiscard]] size_t OffsetForLineCharacter(const std::string& text, int line,
                                            int character);

}  // namespace yac::tool_call
