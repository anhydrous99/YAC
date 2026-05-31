#pragma once

#include <filesystem>

namespace yac::chat {

[[nodiscard]] std::filesystem::path StateDatabasePath(
    const std::filesystem::path& home);
[[nodiscard]] std::filesystem::path StateDatabasePath();

}  // namespace yac::chat
