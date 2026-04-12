#include "chat/env_file.hpp"

#include <algorithm>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace yac::chat {

namespace {

std::string Trim(const std::string& str) {
  const auto is_space = [](char c) {
    return std::isspace(static_cast<unsigned char>(c));
  };

  const auto start = std::find_if_not(str.begin(), str.end(), is_space);
  const auto end =
      std::find_if_not(str.rbegin(), std::string::const_reverse_iterator(start),
                       is_space)
          .base();

  return (start < end) ? std::string(start, end) : std::string();
}

bool IsComment(const std::string& line) {
  const auto trimmed = Trim(line);
  return trimmed.empty() || trimmed[0] == '#';
}

std::pair<std::string, std::string> ParseKeyValue(const std::string& line) {
  const size_t equal_pos = line.find('=');
  if (equal_pos == std::string::npos) {
    return {};
  }

  std::string key = Trim(line.substr(0, equal_pos));
  std::string value = line.substr(equal_pos + 1);

  if (!value.empty() && (value[0] == '"' || value[0] == '\'')) {
    const char quote_char = value[0];
    const size_t closing_pos = value.find(quote_char, 1);
    if (closing_pos != std::string::npos) {
      value = value.substr(1, closing_pos - 1);
    } else {
      value = value.substr(1);
    }
  } else {
    value = Trim(value);
  }

  return {std::move(key), std::move(value)};
}

}  // namespace

std::unordered_map<std::string, std::string> EnvFile::Parse(
    const std::filesystem::path& file_path) {
  std::unordered_map<std::string, std::string> env_vars;

  std::ifstream file(file_path);
  if (!file.is_open()) {
    return env_vars;
  }

  std::string line;
  while (std::getline(file, line)) {
    if (IsComment(line)) {
      continue;
    }

    auto [key, value] = ParseKeyValue(line);
    if (!key.empty()) {
      env_vars[std::move(key)] = std::move(value);
    }
  }

  return env_vars;
}

std::unordered_map<std::string, std::string> EnvFile::FindAndParse() {
  const std::vector<std::filesystem::path> search_paths = {
      std::filesystem::current_path() / ".env",
  };

  for (const auto& path : search_paths) {
    if (std::filesystem::exists(path)) {
      return Parse(path);
    }
  }

  return {};
}

}  // namespace yac::chat
