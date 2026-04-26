#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>

namespace yac::mcp {

class McpDebugLog {
 public:
  explicit McpDebugLog(std::string_view server_id);
  ~McpDebugLog();

  McpDebugLog(const McpDebugLog&) = delete;
  McpDebugLog& operator=(const McpDebugLog&) = delete;
  McpDebugLog(McpDebugLog&&) = delete;
  McpDebugLog& operator=(McpDebugLog&&) = delete;

  void LogFrame(std::string_view direction, std::string_view raw_frame);
  void LogShutdown();

  [[nodiscard]] const std::filesystem::path& Path() const { return path_; }

 private:
  static std::string SanitizeServerId(std::string_view server_id);
  static std::string TimestampIso8601Utc();
  void EnsureParentDirectories();
  void WriteLine(std::string_view line);

  std::filesystem::path path_;
  int fd_ = -1;
  std::mutex mutex_;
};

}  // namespace yac::mcp
