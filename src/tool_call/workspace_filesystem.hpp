#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

namespace yac::tool_call {

inline constexpr size_t kMaxFileBytes = 50UL * 1024UL * 1024UL;

class WorkspaceFilesystem {
 public:
  explicit WorkspaceFilesystem(std::filesystem::path workspace_root);

  [[nodiscard]] const std::filesystem::path& Root() const;
  [[nodiscard]] std::filesystem::path ResolvePath(
      const std::string& path) const;
  [[nodiscard]] std::string DisplayPath(
      const std::filesystem::path& path) const;
  [[nodiscard]] static std::string ReadFile(const std::filesystem::path& path);
  // Reads up to `max_bytes` bytes from the start of `path`. Same symlink
  // refusal and kMaxFileBytes file-size cap as ReadFile, but never reads
  // past `max_bytes` regardless of file size — use this when only a prefix
  // is needed (e.g. for binary detection or capped attachments). Returns
  // fewer than `max_bytes` if the file is shorter.
  [[nodiscard]] static std::string ReadFilePrefix(
      const std::filesystem::path& path, std::size_t max_bytes);
  static void WriteFile(const std::filesystem::path& path,
                        const std::string& content);

 private:
  std::filesystem::path workspace_root_;
};

[[nodiscard]] int CountLines(const std::string& text);
[[nodiscard]] std::string PreviewText(const std::string& text);
[[nodiscard]] std::string TailLines(const std::string& text, size_t max_lines);
[[nodiscard]] size_t OffsetForLineCharacter(const std::string& text, int line,
                                            int character);

}  // namespace yac::tool_call
