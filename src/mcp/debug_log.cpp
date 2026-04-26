#include "mcp/debug_log.hpp"

#include "chat/config_paths.hpp"
#include "mcp/secret_redaction.hpp"

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

#ifndef _WIN32
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace yac::mcp {
namespace {

constexpr auto kDirOwnerOnly = std::filesystem::perms::owner_all;
constexpr auto kFileOwnerOnly =
    std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;

void SetDirectoryPermissions(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::permissions(path, kDirOwnerOnly,
                               std::filesystem::perm_options::replace, ec);
  if (ec) {
    std::string message = "McpDebugLog: cannot set permissions on ";
    message += path.string();
    message += ": ";
    message += ec.message();
    throw std::runtime_error(message);
  }
}

void SetFilePermissions(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::permissions(path, kFileOwnerOnly,
                               std::filesystem::perm_options::replace, ec);
  if (ec) {
    std::string message = "McpDebugLog: cannot set permissions on ";
    message += path.string();
    message += ": ";
    message += ec.message();
    throw std::runtime_error(message);
  }
}

void EnsureDirectory(const std::filesystem::path& path) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    std::string message = "McpDebugLog: cannot create directory ";
    message += path.string();
    message += ": ";
    message += ec.message();
    throw std::runtime_error(message);
  }
#ifndef _WIN32
  SetDirectoryPermissions(path);
#endif
}

std::string JoinLine(std::string_view timestamp, std::string_view direction,
                     std::string_view payload) {
  std::string line;
  line.reserve(timestamp.size() + direction.size() + payload.size() + 3);
  line.append(timestamp);
  line.push_back(' ');
  line.append(direction);
  line.push_back(' ');
  line.append(payload);
  line.push_back('\n');
  return line;
}

}  // namespace

McpDebugLog::McpDebugLog(std::string_view server_id) {
  const auto home = yac::chat::ResolveHomeDir();
  if (!home) {
    throw std::runtime_error("McpDebugLog: cannot resolve home directory");
  }

  const auto logs_dir = yac::chat::GetYacConfigDir(*home) / "logs";
  const auto mcp_dir = logs_dir / "mcp";
  EnsureDirectory(yac::chat::GetYacConfigDir(*home));
  EnsureDirectory(logs_dir);
  EnsureDirectory(mcp_dir);

  path_ = mcp_dir / (SanitizeServerId(server_id) + ".log");

#ifndef _WIN32
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int opened_fd =
      ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
             S_IRUSR | S_IWUSR);
  if (opened_fd < 0) {
    std::string message = "McpDebugLog: cannot open ";
    message += path_.string();
    message += ": ";
    message += std::strerror(errno);
    throw std::runtime_error(message);
  }
  if (::fchmod(opened_fd, S_IRUSR | S_IWUSR) != 0) {
    const auto err = std::string(std::strerror(errno));
    ::close(opened_fd);
    std::string message = "McpDebugLog: cannot set permissions on ";
    message += path_.string();
    message += ": ";
    message += err;
    throw std::runtime_error(message);
  }
  this->fd_ = opened_fd;
#else
  this->fd_ =
      ::open(path_.c_str(), O_WRONLY | O_CREAT | O_APPEND, S_IRUSR | S_IWUSR);
  if (this->fd_ < 0) {
    std::string message = "McpDebugLog: cannot open ";
    message += path_.string();
    throw std::runtime_error(message);
  }
#endif

  SetFilePermissions(path_);
}

McpDebugLog::~McpDebugLog() {
  if (this->fd_ >= 0) {
#ifndef _WIN32
    ::close(this->fd_);
#endif
  }
}

std::string McpDebugLog::SanitizeServerId(std::string_view server_id) {
  std::string result;
  result.reserve(server_id.size());
  for (char c : server_id) {
    if (std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '-' ||
        c == '_') {
      result += c;
    } else {
      result += '_';
    }
  }
  if (result.empty()) {
    result = "server";
  }
  return result;
}

std::string McpDebugLog::TimestampIso8601Utc() {
  using clock = std::chrono::system_clock;
  const auto now = clock::now();
  const auto seconds = clock::to_time_t(now);
  std::tm tm{};
#ifndef _WIN32
  gmtime_r(&seconds, &tm);
#else
  gmtime_s(&tm, &seconds);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%SZ");
  return out.str();
}

void McpDebugLog::EnsureParentDirectories() {
  if (!path_.has_parent_path()) {
    return;
  }
  EnsureDirectory(path_.parent_path());
}

void McpDebugLog::WriteLine(std::string_view line) {
  std::lock_guard<std::mutex> lock(mutex_);
  EnsureParentDirectories();
  std::size_t written = 0;
  while (written < line.size()) {
#ifndef _WIN32
    const auto bytes =
        ::write(this->fd_, line.data() + written, line.size() - written);
    if (bytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      std::string message = "McpDebugLog: failed writing ";
      message += path_.string();
      throw std::runtime_error(message);
    }
    written += static_cast<std::size_t>(bytes);
#else
    throw std::runtime_error("McpDebugLog: unsupported platform");
#endif
  }
#ifndef _WIN32
  if (::fsync(this->fd_) != 0) {
    std::string message = "McpDebugLog: failed flushing ";
    message += path_.string();
    throw std::runtime_error(message);
  }
#endif
}

void McpDebugLog::LogFrame(std::string_view direction,
                           std::string_view raw_frame) {
  const std::string redacted = RedactSecrets(raw_frame);
  WriteLine(JoinLine(TimestampIso8601Utc(), direction, redacted));
}

void McpDebugLog::LogShutdown() {
  std::string payload = R"({"event":"connection_closed","ts":")";
  payload += TimestampIso8601Utc();
  payload += R"("})";
  WriteLine(payload + "\n");
}

}  // namespace yac::mcp
