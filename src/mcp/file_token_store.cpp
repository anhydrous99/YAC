#include "mcp/file_token_store.hpp"

#include "chat/config_paths.hpp"

#include <cctype>
#include <cerrno>
#include <cstring>
#include <fstream>
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

std::string SanitizeServerId(std::string_view server_id) {
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
  return result;
}

}  // namespace

FileTokenStore::FileTokenStore() {
  const auto home = yac::chat::ResolveHomeDir();
  if (!home) {
    throw std::runtime_error("FileTokenStore: cannot resolve home directory");
  }
  base_dir_ = yac::chat::GetYacConfigDir(*home) / "mcp" / "auth";
}

FileTokenStore::FileTokenStore(std::filesystem::path base_dir)
    : base_dir_(std::move(base_dir)) {}

std::filesystem::path FileTokenStore::TokenPath(
    std::string_view server_id) const {
  return base_dir_ / (SanitizeServerId(server_id) + ".json");
}

void FileTokenStore::EnsureBaseDir() const {
  std::error_code ec;
  std::filesystem::create_directories(base_dir_, ec);
  if (ec) {
    throw std::runtime_error("FileTokenStore: cannot create directory " +
                             base_dir_.string() + ": " + ec.message());
  }
#ifndef _WIN32
  std::filesystem::permissions(base_dir_, std::filesystem::perms::owner_all,
                               std::filesystem::perm_options::replace, ec);
  if (ec) {
    throw std::runtime_error("FileTokenStore: cannot set permissions on " +
                             base_dir_.string() + ": " + ec.message());
  }
#endif
}

std::optional<std::string> FileTokenStore::Get(
    std::string_view server_id) const {
  const auto path = TokenPath(server_id);

  std::error_code ec;
  if (!std::filesystem::exists(path, ec) || ec) {
    return std::nullopt;
  }

#ifndef _WIN32
  const auto file_status = std::filesystem::status(path, ec);
  if (ec) {
    throw std::runtime_error("FileTokenStore: cannot stat " + path.string() +
                             ": " + ec.message());
  }
  constexpr auto kExpected =
      std::filesystem::perms::owner_read | std::filesystem::perms::owner_write;
  const auto file_perms =
      file_status.permissions() & std::filesystem::perms::all;
  if (file_perms != kExpected) {
    throw std::runtime_error(
        "FileTokenStore: refusing to read " + path.string() +
        " — file permissions are too open (expected 0600). "
        "Fix with: chmod 0600 " +
        path.string());
  }
#endif

  std::ifstream file(path);
  if (!file) {
    throw std::runtime_error("FileTokenStore: cannot open " + path.string());
  }
  std::ostringstream buf;
  buf << file.rdbuf();
  return buf.str();
}

void FileTokenStore::Set(std::string_view server_id,
                         std::string_view token_json) {
  EnsureBaseDir();
  const auto path = TokenPath(server_id);
  const auto tmp_path = std::filesystem::path(path.string() + ".tmp");

#ifndef _WIN32
  const char* const tmp_cstr = tmp_path.c_str();
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int fd = ::open(tmp_cstr, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                        S_IRUSR | S_IWUSR);
  if (fd < 0) {
    throw std::runtime_error("FileTokenStore: cannot open tmp file " +
                             tmp_path.string() + ": " + std::strerror(errno));
  }

  if (::fchmod(fd, S_IRUSR | S_IWUSR) != 0) {
    const auto err = std::string(std::strerror(errno));
    ::close(fd);
    throw std::runtime_error("FileTokenStore: fchmod failed on " +
                             tmp_path.string() + ": " + err);
  }

  std::size_t written = 0;
  while (written < token_json.size()) {
    const auto bytes =
        ::write(fd, token_json.data() + written, token_json.size() - written);
    if (bytes < 0) {
      if (errno == EINTR) {
        continue;
      }
      const auto err = std::string(std::strerror(errno));
      ::close(fd);
      throw std::runtime_error("FileTokenStore: write failed on " +
                               tmp_path.string() + ": " + err);
    }
    written += static_cast<std::size_t>(bytes);
  }

  if (::fsync(fd) != 0) {
    const auto err = std::string(std::strerror(errno));
    ::close(fd);
    throw std::runtime_error("FileTokenStore: fsync failed on " +
                             tmp_path.string() + ": " + err);
  }

  if (::close(fd) != 0) {
    throw std::runtime_error("FileTokenStore: close failed on " +
                             tmp_path.string() + ": " + std::strerror(errno));
  }
#else
  {
    std::ofstream out(tmp_path, std::ios::trunc);
    if (!out) {
      throw std::runtime_error("FileTokenStore: cannot open tmp file " +
                               tmp_path.string());
    }
    out << token_json;
    out.close();
    if (!out) {
      throw std::runtime_error("FileTokenStore: write failed on " +
                               tmp_path.string());
    }
  }
#endif

  std::error_code ec;
  std::filesystem::rename(tmp_path, path, ec);
  if (ec) {
    std::filesystem::remove(tmp_path);
    throw std::runtime_error("FileTokenStore: rename failed: " + ec.message());
  }
}

void FileTokenStore::Erase(std::string_view server_id) {
  const auto path = TokenPath(server_id);
  std::error_code ec;
  std::filesystem::remove(path, ec);
}

}  // namespace yac::mcp
