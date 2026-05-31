#include "chat/state_paths.hpp"

#include "chat/config_paths.hpp"

#include <stdexcept>

namespace yac::chat {

namespace {

constexpr const char* kStateDatabaseFileName = "state.sqlite";

}

std::filesystem::path StateDatabasePath(const std::filesystem::path& home) {
  return GetYacConfigDir(home) / kStateDatabaseFileName;
}

std::filesystem::path StateDatabasePath() {
  const auto home = ResolveHomeDir();
  if (!home) {
    throw std::runtime_error("Cannot determine home directory for YAC state");
  }
  return StateDatabasePath(*home);
}

}  // namespace yac::chat
