#include "tool_call/gitignore_filter.hpp"

#include <filesystem>
#include <fnmatch.h>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace yac::tool_call {

namespace {

std::string Trim(std::string_view sv) {
  size_t start = sv.find_first_not_of(" \t\r\n");
  if (start == std::string_view::npos) return "";
  size_t end = sv.find_last_not_of(" \t\r\n");
  return std::string(sv.substr(start, end - start + 1));
}

void PushPattern(std::vector<std::string>& target, std::string pattern) {
  target.push_back(std::move(pattern));
}

void ParseLine(std::string_view line_view, std::vector<std::string>& patterns,
               std::vector<std::string>& negations) {
  std::string line = Trim(line_view);
  if (line.empty() || line[0] == '#') return;

  bool is_negation = line[0] == '!';
  if (is_negation) {
    line = line.substr(1);
    if (line.empty()) return;
  }

  auto& target = is_negation ? negations : patterns;

  if (!line.empty() && line.back() == '/') {
    line.pop_back();
    PushPattern(target, line + "/**");
    PushPattern(target, line);
  } else if (!line.empty() && line[0] == '/') {
    line = line.substr(1);
    PushPattern(target, line);
    PushPattern(target, line + "/**");
  } else {
    PushPattern(target, line);
  }
}

const std::vector<std::string>& FallbackDenyList() {
  static const std::vector<std::string> kList = {
      ".git",        ".git/**",        "node_modules", "node_modules/**",
      "build",       "build/**",       "dist",         "dist/**",
      "__pycache__", "__pycache__/**", ".cache",       ".cache/**",
      "vendor",      "vendor/**",      "target",       "target/**",
      ".venv",       ".venv/**",       ".next",        ".next/**",
      ".nuxt",       ".nuxt/**",       "*.o",          "*.so",
      "*.dylib",     "*.exe",          "*.pyc",        "*.class",
  };
  return kList;
}

}  // namespace

GitignoreFilter::GitignoreFilter(std::filesystem::path workspace_root)
    : root_(std::move(workspace_root)) {
  std::ifstream file(root_ / ".gitignore");
  if (file.is_open()) {
    std::string line;
    while (std::getline(file, line)) {
      ParseLine(line, patterns_, negations_);
    }
  } else {
    using_fallback_denylist_ = true;
    patterns_ = FallbackDenyList();
  }
}

bool GitignoreFilter::ShouldSkip(std::string_view relative_path) const {
  const std::string path(relative_path);
  bool matched = false;
  for (const auto& pat : patterns_) {
    if (fnmatch(pat.c_str(), path.c_str(), 0) == 0) {
      matched = true;
      break;
    }
  }
  if (!matched) return false;
  for (const auto& neg : negations_) {
    if (fnmatch(neg.c_str(), path.c_str(), 0) == 0) {
      return false;
    }
  }
  return true;
}

}  // namespace yac::tool_call
