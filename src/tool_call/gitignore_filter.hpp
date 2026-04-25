#pragma once
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace yac::tool_call {

class GitignoreFilter {
 public:
  // Construct from workspace root. If <root>/.gitignore exists, parse it.
  // Otherwise, use built-in deny-list.
  explicit GitignoreFilter(std::filesystem::path workspace_root);

  // Returns true if `relative_path` (relative to workspace root) should be
  // skipped.
  [[nodiscard]] bool ShouldSkip(std::string_view relative_path) const;

 private:
  std::filesystem::path root_;
  std::vector<std::string> patterns_;   // patterns that cause skip
  std::vector<std::string> negations_;  // patterns starting with ! that un-skip
  bool using_fallback_denylist_{false};
};

}  // namespace yac::tool_call
