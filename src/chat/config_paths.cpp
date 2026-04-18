#include "chat/config_paths.hpp"

#include <cstdlib>
#include <stdexcept>

#ifndef _WIN32
#include <pwd.h>
#include <unistd.h>
#endif

namespace yac::chat {

namespace {

constexpr const char* kConfigDirName = ".yac";
constexpr const char* kSettingsFileName = "settings.toml";

}  // namespace

std::optional<std::filesystem::path> ResolveHomeDir() {
  if (const char* home = std::getenv("HOME")) {
    if (*home != '\0') {
      return std::filesystem::path(home);
    }
  }
#ifndef _WIN32
  if (const passwd* pw = ::getpwuid(::getuid())) {
    if (pw->pw_dir != nullptr && *pw->pw_dir != '\0') {
      return std::filesystem::path(pw->pw_dir);
    }
  }
#else
  if (const char* userprofile = std::getenv("USERPROFILE")) {
    if (*userprofile != '\0') {
      return std::filesystem::path(userprofile);
    }
  }
#endif
  return std::nullopt;
}

std::filesystem::path GetYacConfigDir(const std::filesystem::path& home) {
  return home / kConfigDirName;
}

std::filesystem::path GetSettingsPath(const std::filesystem::path& home) {
  return GetYacConfigDir(home) / kSettingsFileName;
}

std::filesystem::path GetYacConfigDir() {
  auto home = ResolveHomeDir();
  if (!home) {
    throw std::runtime_error("Cannot determine home directory for YAC config");
  }
  return GetYacConfigDir(*home);
}

std::filesystem::path GetSettingsPath() {
  auto home = ResolveHomeDir();
  if (!home) {
    throw std::runtime_error("Cannot determine home directory for YAC config");
  }
  return GetSettingsPath(*home);
}

}  // namespace yac::chat
