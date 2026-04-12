#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>

namespace yac::chat {

class EnvFile {
 public:
  [[nodiscard]] static std::unordered_map<std::string, std::string> Parse(
      const std::filesystem::path& file_path);

  [[nodiscard]] static std::unordered_map<std::string, std::string>
  FindAndParse();
};

}  // namespace yac::chat
